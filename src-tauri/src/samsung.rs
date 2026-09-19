//! Samsung Smart TV remote over Device Connect (websocket).
//!
//! Living-room path: zappe-tv → Wi-Fi → TV `ws://host:8001` (or `wss://:8002`).
//! No HDMI-CEC (`/dev/cec0` is missing on the Beelink). Volume / mute are TV
//! keys (`KEY_VOLUP` / `KEY_VOLDOWN` / `KEY_MUTE`) — the same Device Connect
//! channel the Samsung remote uses. Power and HDMI source are **not** sent
//! from here (Power stays “return to guide”; flipping inputs would drop HDMI).
//!
//! Token lives in env / `~/.local/share/zappe/samsung.json` — never git.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::Context;
use base64::Engine;
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{connect_async, connect_async_tls_with_config, Connector};

use crate::paths::{data_dir, ensure_data_dir};

pub const DEFAULT_HOST: &str = "192.168.3.6";
pub const DEFAULT_PORT: u16 = 8001;
pub const DEFAULT_SECURE_PORT: u16 = 8002;
pub const DEFAULT_NAME: &str = "Zappe";
pub const EVENT_TV_CONTROL: &str = "tv-control";

const PAIR_TIMEOUT: Duration = Duration::from_secs(12);
const KEY_SETTLE: Duration = Duration::from_millis(150);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TvKey {
    VolumeUp,
    VolumeDown,
    Mute,
}

impl TvKey {
    pub fn samsung_code(self) -> &'static str {
        match self {
            Self::VolumeUp => "KEY_VOLUP",
            Self::VolumeDown => "KEY_VOLDOWN",
            Self::Mute => "KEY_MUTE",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SamsungConfig {
    pub host: String,
    pub port: u16,
    pub secure_port: u16,
    pub token: Option<String>,
    pub name: String,
    pub disabled: bool,
}

impl SamsungConfig {
    pub fn appliance_default() -> Self {
        Self {
            host: DEFAULT_HOST.into(),
            port: DEFAULT_PORT,
            secure_port: DEFAULT_SECURE_PORT,
            token: None,
            name: DEFAULT_NAME.into(),
            disabled: false,
        }
    }

    pub fn enabled(&self) -> bool {
        !self.disabled && !self.host.trim().is_empty()
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct SamsungFile {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
    #[serde(
        default,
        alias = "securePort",
        alias = "secure_port",
        skip_serializing_if = "Option::is_none"
    )]
    pub secure_port: Option<u16>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub token: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TvError {
    Disabled,
    Timeout,
    Connect(String),
    Unauthorized,
    PairingRequired,
    Protocol(String),
}

impl TvError {
    pub fn is_transport(&self) -> bool {
        matches!(self, Self::Timeout | Self::Connect(_))
    }

    pub fn user_message(&self, cfg: &SamsungConfig) -> String {
        let dest = format!("{}:{}", cfg.host, cfg.port);
        match self {
            Self::Unauthorized | Self::PairingRequired => format!(
                "Aceite o Zappe no Device Connect da TV Samsung ({dest}). Volume no PulseAudio até lá."
            ),
            Self::Timeout | Self::Connect(_) => {
                format!("TV Samsung offline ({dest}) — volume no PulseAudio do aparelho.")
            }
            Self::Disabled => "Controle Samsung desligado — volume no PulseAudio.".into(),
            Self::Protocol(msg) => format!("TV Samsung ({dest}): {msg} — volume no PulseAudio."),
        }
    }
}

impl std::fmt::Display for TvError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Disabled => write!(f, "samsung disabled"),
            Self::Timeout => write!(f, "timeout waiting for Device Connect"),
            Self::Connect(msg) => write!(f, "connect: {msg}"),
            Self::Unauthorized => write!(f, "Device Connect unauthorized — allow Zappe on the TV"),
            Self::PairingRequired => write!(f, "Device Connect pairing required"),
            Self::Protocol(msg) => write!(f, "protocol: {msg}"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ChannelEvent {
    Connect { token: Option<String> },
    Unauthorized,
    Timeout,
    Other(String),
}

pub fn config_search_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    paths.push(data_dir().join("samsung.json"));
    if let Some(cfg) = dirs::config_dir() {
        paths.push(cfg.join("zappe").join("samsung.json"));
    }
    paths
}

pub fn primary_config_path() -> PathBuf {
    data_dir().join("samsung.json")
}

fn env_flag(name: &str) -> bool {
    matches!(
        std::env::var(name)
            .map(|s| s.to_ascii_lowercase())
            .as_deref(),
        Ok("1") | Ok("true") | Ok("yes")
    )
}

fn overlay_file(cfg: &mut SamsungConfig, file: &SamsungFile) {
    if let Some(host) = file
        .host
        .as_deref()
        .map(str::trim)
        .filter(|s| !s.is_empty())
    {
        cfg.host = host.to_string();
    }
    if let Some(port) = file.port {
        cfg.port = port;
    }
    if let Some(port) = file.secure_port {
        cfg.secure_port = port;
    }
    if let Some(token) = file
        .token
        .as_deref()
        .map(str::trim)
        .filter(|s| !s.is_empty())
    {
        cfg.token = Some(token.to_string());
    }
    if let Some(name) = file
        .name
        .as_deref()
        .map(str::trim)
        .filter(|s| !s.is_empty())
    {
        cfg.name = name.to_string();
    }
}

pub fn read_samsung_file(path: &Path) -> Option<SamsungFile> {
    let bytes = fs::read(path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

pub fn load_config() -> SamsungConfig {
    let mut cfg = SamsungConfig::appliance_default();
    // Search list is data-dir first; overlay oldest-to-newest so samsung.json wins.
    for path in config_search_paths().into_iter().rev() {
        if let Some(file) = read_samsung_file(&path) {
            overlay_file(&mut cfg, &file);
        }
    }
    if let Ok(host) = std::env::var("ZAPPE_SAMSUNG_HOST") {
        let host = host.trim();
        if host.is_empty() {
            cfg.host.clear();
        } else {
            cfg.host = host.to_string();
        }
    }
    if let Ok(port) = std::env::var("ZAPPE_SAMSUNG_PORT") {
        if let Ok(port) = port.trim().parse() {
            cfg.port = port;
        }
    }
    if let Ok(port) = std::env::var("ZAPPE_SAMSUNG_SECURE_PORT") {
        if let Ok(port) = port.trim().parse() {
            cfg.secure_port = port;
        }
    }
    if let Ok(token) = std::env::var("ZAPPE_SAMSUNG_TOKEN") {
        let token = token.trim();
        if token.is_empty() {
            cfg.token = None;
        } else {
            cfg.token = Some(token.to_string());
        }
    }
    if let Ok(name) = std::env::var("ZAPPE_SAMSUNG_NAME") {
        let name = name.trim();
        if !name.is_empty() {
            cfg.name = name.to_string();
        }
    }
    cfg.disabled = env_flag("ZAPPE_SAMSUNG_DISABLE") || cfg.host.trim().is_empty();
    cfg
}

fn encode_name(name: &str) -> String {
    base64::engine::general_purpose::STANDARD.encode(name.as_bytes())
}

pub fn websocket_url(cfg: &SamsungConfig, secure: bool, token: Option<&str>) -> String {
    let scheme = if secure { "wss" } else { "ws" };
    let port = if secure { cfg.secure_port } else { cfg.port };
    let name = encode_name(&cfg.name);
    let mut url = format!(
        "{scheme}://{}:{port}/api/v2/channels/samsung.remote.control?name={name}",
        cfg.host
    );
    if let Some(token) = token.map(str::trim).filter(|s| !s.is_empty()) {
        url.push_str("&token=");
        url.push_str(token);
    }
    url
}

pub fn remote_key_payload(key: TvKey) -> String {
    serde_json::json!({
        "method": "ms.remote.control",
        "params": {
            "Cmd": "Click",
            "DataOfCmd": key.samsung_code(),
            "Option": "false",
            "TypeOfRemote": "SendRemoteKey"
        }
    })
    .to_string()
}

pub fn parse_channel_event(raw: &str) -> Result<ChannelEvent, TvError> {
    let v: serde_json::Value =
        serde_json::from_str(raw).map_err(|err| TvError::Protocol(err.to_string()))?;
    let event = v
        .get("event")
        .and_then(|e| e.as_str())
        .unwrap_or("")
        .to_string();
    match event.as_str() {
        "ms.channel.connect" => {
            let token = v.pointer("/data/token").and_then(|t| match t {
                serde_json::Value::String(s) if !s.trim().is_empty() => Some(s.clone()),
                serde_json::Value::Number(n) => Some(n.to_string()),
                _ => None,
            });
            Ok(ChannelEvent::Connect { token })
        }
        "ms.channel.unauthorized" => Ok(ChannelEvent::Unauthorized),
        "ms.channel.timeOut" | "ms.channel.timeout" => Ok(ChannelEvent::Timeout),
        other => Ok(ChannelEvent::Other(other.to_string())),
    }
}

pub fn persist_token(token: &str) -> anyhow::Result<PathBuf> {
    let token = token.trim();
    anyhow::ensure!(!token.is_empty(), "empty token");
    ensure_data_dir()?;
    let path = primary_config_path();
    let mut file = read_samsung_file(&path).unwrap_or_default();
    if file.host.is_none() {
        file.host = Some(load_config().host);
    }
    if file.port.is_none() {
        file.port = Some(load_config().port);
    }
    file.token = Some(token.to_string());
    if file.name.is_none() {
        file.name = Some(DEFAULT_NAME.into());
    }
    let json = serde_json::to_string_pretty(&file)?;
    let tmp = path.with_extension("json.tmp");
    {
        let mut out =
            fs::File::create(&tmp).with_context(|| format!("create {}", tmp.display()))?;
        out.write_all(json.as_bytes())?;
        out.write_all(b"\n")?;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(&tmp, fs::Permissions::from_mode(0o600));
    }
    fs::rename(&tmp, &path).with_context(|| format!("write {}", path.display()))?;
    Ok(path)
}

fn send_timeout() -> Duration {
    std::env::var("ZAPPE_SAMSUNG_TIMEOUT_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .map(Duration::from_millis)
        .unwrap_or(Duration::from_millis(1800))
}

type WsStream =
    tokio_tungstenite::WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>;

async fn connect_ws(url: &str, secure: bool) -> Result<WsStream, TvError> {
    if secure {
        let tls = native_tls::TlsConnector::builder()
            .danger_accept_invalid_certs(true)
            .danger_accept_invalid_hostnames(true)
            .build()
            .map_err(|err| TvError::Connect(err.to_string()))?;
        let (ws, _) =
            connect_async_tls_with_config(url, None, false, Some(Connector::NativeTls(tls)))
                .await
                .map_err(|err| TvError::Connect(err.to_string()))?;
        Ok(ws)
    } else {
        let (ws, _) = connect_async(url)
            .await
            .map_err(|err| TvError::Connect(err.to_string()))?;
        Ok(ws)
    }
}

async fn wait_connected(ws: &mut WsStream) -> Result<Option<String>, TvError> {
    loop {
        let msg = ws
            .next()
            .await
            .ok_or_else(|| TvError::Protocol("socket closed before ms.channel.connect".into()))?
            .map_err(|err| TvError::Protocol(err.to_string()))?;
        match msg {
            Message::Text(text) => match parse_channel_event(&text.to_string())? {
                ChannelEvent::Connect { token } => return Ok(token),
                ChannelEvent::Unauthorized => return Err(TvError::Unauthorized),
                ChannelEvent::Timeout => return Err(TvError::PairingRequired),
                ChannelEvent::Other(_) => continue,
            },
            Message::Ping(p) => {
                let _ = ws.send(Message::Pong(p)).await;
            }
            Message::Close(_) => {
                return Err(TvError::Protocol("socket closed".into()));
            }
            _ => continue,
        }
    }
}

async fn send_key_on(
    cfg: &SamsungConfig,
    key: Option<TvKey>,
    secure: bool,
) -> Result<Option<String>, TvError> {
    if !cfg.enabled() {
        return Err(TvError::Disabled);
    }
    let url = websocket_url(cfg, secure, cfg.token.as_deref());
    let mut ws = connect_ws(&url, secure).await?;
    let token = wait_connected(&mut ws).await?;
    if let Some(key) = key {
        ws.send(Message::Text(remote_key_payload(key).into()))
            .await
            .map_err(|err| TvError::Protocol(err.to_string()))?;
        tokio::time::sleep(KEY_SETTLE).await;
    }
    let _ = ws.close(None).await;
    Ok(token)
}

/// Send one TV key. Tries `ws://:8001` then `wss://:8002` (self-signed).
/// Returns a newly issued Device Connect token when the TV sends one.
pub async fn send_key(cfg: &SamsungConfig, key: TvKey) -> Result<Option<String>, TvError> {
    if !cfg.enabled() {
        return Err(TvError::Disabled);
    }
    let timeout = send_timeout();
    let attempt = async {
        match send_key_on(cfg, Some(key), false).await {
            Ok(token) => Ok(token),
            Err(err) if err.is_transport() => {
                log::debug!(
                    "Samsung ws://{}:{} failed ({err}); trying wss",
                    cfg.host,
                    cfg.port
                );
                send_key_on(cfg, Some(key), true).await
            }
            Err(err) => Err(err),
        }
    };
    tokio::time::timeout(timeout, attempt)
        .await
        .map_err(|_| TvError::Timeout)?
}

/// Connect without sending a key — used at startup to pair / refresh the token.
pub async fn pair(cfg: &SamsungConfig) -> Result<Option<String>, TvError> {
    if !cfg.enabled() {
        return Err(TvError::Disabled);
    }
    let attempt = async {
        match send_key_on(cfg, None, false).await {
            Ok(token) => Ok(token),
            Err(err) if err.is_transport() => send_key_on(cfg, None, true).await,
            Err(err) => Err(err),
        }
    };
    tokio::time::timeout(PAIR_TIMEOUT, attempt)
        .await
        .map_err(|_| TvError::Timeout)?
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProbeInfo {
    pub name: String,
    pub model: String,
    pub token_auth: bool,
}

impl ProbeInfo {
    pub fn summary(&self) -> String {
        let mut bits = Vec::new();
        if !self.model.is_empty() {
            bits.push(self.model.clone());
        }
        if !self.name.is_empty() && self.name != self.model {
            bits.push(self.name.clone());
        }
        if self.token_auth {
            bits.push("TokenAuthSupport".into());
        }
        if bits.is_empty() {
            "Device Connect".into()
        } else {
            bits.join(" · ")
        }
    }
}

fn parse_http_body(raw: &[u8]) -> Option<&[u8]> {
    let text = std::str::from_utf8(raw).ok()?;
    let idx = text.find("\r\n\r\n")?;
    Some(&raw[idx + 4..])
}

pub fn parse_probe_json(body: &str) -> ProbeInfo {
    let v: serde_json::Value = serde_json::from_str(body).unwrap_or(serde_json::Value::Null);
    let device = v.get("device").cloned().unwrap_or(serde_json::Value::Null);
    let token_auth = device
        .get("TokenAuthSupport")
        .and_then(|x| x.as_str())
        .is_some_and(|s| s.eq_ignore_ascii_case("true"))
        || device
            .get("TokenAuthSupport")
            .and_then(|x| x.as_bool())
            .unwrap_or(false);
    ProbeInfo {
        name: v
            .get("name")
            .and_then(|x| x.as_str())
            .or_else(|| device.get("name").and_then(|x| x.as_str()))
            .unwrap_or("")
            .to_string(),
        model: device
            .get("modelName")
            .or_else(|| device.get("model"))
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        token_auth,
    }
}

/// GET `http://host:8001/api/v2/` — cheap “is the TV on” check.
pub async fn probe_http(cfg: &SamsungConfig) -> Result<ProbeInfo, TvError> {
    if !cfg.enabled() {
        return Err(TvError::Disabled);
    }
    let timeout = Duration::from_millis(800);
    let mut stream =
        tokio::time::timeout(timeout, TcpStream::connect((cfg.host.as_str(), cfg.port)))
            .await
            .map_err(|_| TvError::Timeout)?
            .map_err(|err| TvError::Connect(err.to_string()))?;
    let req = format!(
        "GET /api/v2/ HTTP/1.1\r\nHost: {}:{}\r\nConnection: close\r\n\r\n",
        cfg.host, cfg.port
    );
    let buf = tokio::time::timeout(timeout, async {
        stream
            .write_all(req.as_bytes())
            .await
            .map_err(|err| TvError::Connect(err.to_string()))?;
        let mut buf = Vec::new();
        stream
            .read_to_end(&mut buf)
            .await
            .map_err(|err| TvError::Connect(err.to_string()))?;
        Ok::<_, TvError>(buf)
    })
    .await
    .map_err(|_| TvError::Timeout)??;
    let body = parse_http_body(&buf).unwrap_or(&buf);
    let text = String::from_utf8_lossy(body);
    if text.trim().is_empty() {
        return Err(TvError::Protocol("empty /api/v2/ body".into()));
    }
    Ok(parse_probe_json(&text))
}

#[cfg(test)]
pub(crate) static ENV_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

pub async fn startup_probe() {
    let cfg = load_config();
    if !cfg.enabled() {
        log::info!("Samsung TV control disabled (ZAPPE_SAMSUNG_DISABLE or empty host)");
        return;
    }
    match probe_http(&cfg).await {
        Ok(info) => {
            log::info!(
                "Samsung TV at {}:{} — {}",
                cfg.host,
                cfg.port,
                info.summary()
            );
            match pair(&cfg).await {
                Ok(token) => {
                    if let Some(token) = token.as_deref().filter(|t| !t.is_empty()) {
                        match persist_token(token) {
                            Ok(path) => log::info!(
                                "Samsung Device Connect token saved to {}",
                                path.display()
                            ),
                            Err(err) => log::warn!("could not save Samsung token: {err:#}"),
                        }
                    } else {
                        log::info!("Samsung Device Connect ready (no new token)");
                    }
                }
                Err(err) => {
                    log::warn!(
                        "Samsung Device Connect: {err} — allow the client on the TV, then press VOL. Until then VOL/Mute use pactl."
                    );
                }
            }
        }
        Err(err) => {
            log::info!(
                "Samsung TV not reachable at {}:{} ({err}) — VOL/Mute fall back to pactl until the TV is on and Device Connect allows Zappe",
                cfg.host,
                cfg.port
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    fn lock_env() -> std::sync::MutexGuard<'static, ()> {
        ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn restore_env(key: &str, prev: Option<std::ffi::OsString>) {
        match prev {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }
    }

    #[test]
    fn volume_keys_are_samsung_remote_codes() {
        assert_eq!(TvKey::VolumeUp.samsung_code(), "KEY_VOLUP");
        assert_eq!(TvKey::VolumeDown.samsung_code(), "KEY_VOLDOWN");
        assert_eq!(TvKey::Mute.samsung_code(), "KEY_MUTE");
        let payload = remote_key_payload(TvKey::VolumeUp);
        assert!(payload.contains("KEY_VOLUP"), "{payload}");
        assert!(payload.contains("ms.remote.control"), "{payload}");
        assert!(payload.contains("SendRemoteKey"), "{payload}");
    }

    #[test]
    fn websocket_url_includes_base64_name_and_token() {
        let cfg = SamsungConfig {
            host: "192.168.3.6".into(),
            port: 8001,
            secure_port: 8002,
            token: Some("tok123".into()),
            name: "Zappe".into(),
            disabled: false,
        };
        let ws = websocket_url(&cfg, false, cfg.token.as_deref());
        assert!(ws.starts_with("ws://192.168.3.6:8001/api/v2/channels/samsung.remote.control?"));
        assert!(ws.contains(&encode_name("Zappe")), "{ws}");
        assert!(ws.contains("token=tok123"), "{ws}");
        let wss = websocket_url(&cfg, true, None);
        assert!(wss.starts_with("wss://192.168.3.6:8002/"));
        assert!(!wss.contains("token="), "{wss}");
    }

    #[test]
    fn parse_connect_and_unauthorized() {
        let connect = r#"{"event":"ms.channel.connect","data":{"token":"998877"}}"#;
        assert_eq!(
            parse_channel_event(connect).unwrap(),
            ChannelEvent::Connect {
                token: Some("998877".into())
            }
        );
        let numeric = r#"{"event":"ms.channel.connect","data":{"token":1234}}"#;
        assert_eq!(
            parse_channel_event(numeric).unwrap(),
            ChannelEvent::Connect {
                token: Some("1234".into())
            }
        );
        assert_eq!(
            parse_channel_event(r#"{"event":"ms.channel.unauthorized"}"#).unwrap(),
            ChannelEvent::Unauthorized
        );
        assert_eq!(
            parse_channel_event(r#"{"event":"ms.channel.timeOut"}"#).unwrap(),
            ChannelEvent::Timeout
        );
    }

    #[test]
    fn probe_json_reads_model_and_token_auth() {
        let body = r#"{
            "name": "[TV] Samsung",
            "device": {"modelName": "UN55NU7100", "TokenAuthSupport": "true"}
        }"#;
        let info = parse_probe_json(body);
        assert_eq!(info.model, "UN55NU7100");
        assert!(info.token_auth);
        assert!(info.summary().contains("UN55NU7100"));
    }

    #[test]
    fn load_config_overlays_env_and_can_disable() {
        let _g = lock_env();
        let prev_host = std::env::var_os("ZAPPE_SAMSUNG_HOST");
        let prev_token = std::env::var_os("ZAPPE_SAMSUNG_TOKEN");
        let prev_dis = std::env::var_os("ZAPPE_SAMSUNG_DISABLE");
        let prev_data = std::env::var_os("ZAPPE_DATA_DIR");
        std::env::set_var("ZAPPE_DATA_DIR", "/tmp/zappe-samsung-missing");
        std::env::set_var("ZAPPE_SAMSUNG_HOST", "10.0.0.9");
        std::env::set_var("ZAPPE_SAMSUNG_TOKEN", "secret-from-env");
        std::env::remove_var("ZAPPE_SAMSUNG_DISABLE");
        let cfg = load_config();
        assert_eq!(cfg.host, "10.0.0.9");
        assert_eq!(cfg.token.as_deref(), Some("secret-from-env"));
        assert!(cfg.enabled());
        std::env::set_var("ZAPPE_SAMSUNG_DISABLE", "1");
        assert!(!load_config().enabled());
        std::env::remove_var("ZAPPE_SAMSUNG_DISABLE");
        std::env::set_var("ZAPPE_SAMSUNG_HOST", "");
        assert!(!load_config().enabled());
        restore_env("ZAPPE_SAMSUNG_HOST", prev_host);
        restore_env("ZAPPE_SAMSUNG_TOKEN", prev_token);
        restore_env("ZAPPE_SAMSUNG_DISABLE", prev_dis);
        restore_env("ZAPPE_DATA_DIR", prev_data);
    }

    #[test]
    fn persist_token_writes_data_dir_and_keeps_mode_private() {
        let _g = lock_env();
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        let dir = std::env::temp_dir().join(format!(
            "zappe-samsung-token-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or(0)
        ));
        let _ = fs::create_dir_all(&dir);
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        let path = persist_token("living-room-token").expect("persist");
        assert_eq!(path, dir.join("samsung.json"));
        let text = fs::read_to_string(&path).unwrap();
        assert!(text.contains("living-room-token"));
        assert!(!text.contains("ZAPPE_SAMSUNG_TOKEN"));
        let file: SamsungFile = serde_json::from_str(&text).unwrap();
        assert_eq!(file.token.as_deref(), Some("living-room-token"));
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(&path).unwrap().permissions().mode() & 0o777;
            assert_eq!(mode, 0o600, "token file must not be world-readable");
        }
        restore_env("ZAPPE_DATA_DIR", prev);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn fallback_copy_names_device_connect() {
        let cfg = SamsungConfig::appliance_default();
        let msg = TvError::Unauthorized.user_message(&cfg);
        assert!(msg.contains("192.168.3.6:8001"), "{msg}");
        assert!(msg.contains("Device Connect"), "{msg}");
        assert!(msg.contains("PulseAudio"), "{msg}");
    }

    #[tokio::test]
    async fn mock_tv_accepts_volup_and_returns_token() {
        let received = Arc::new(Mutex::new(Vec::<String>::new()));
        let keys = received.clone();
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut ws = tokio_tungstenite::accept_async(stream).await.unwrap();
            ws.send(Message::Text(
                r#"{"event":"ms.channel.connect","data":{"token":"pair-1"}}"#.into(),
            ))
            .await
            .unwrap();
            if let Some(Ok(Message::Text(text))) = ws.next().await {
                let v: serde_json::Value = serde_json::from_str(&text.to_string()).unwrap();
                if let Some(code) = v.pointer("/params/DataOfCmd").and_then(|x| x.as_str()) {
                    keys.lock().unwrap().push(code.to_string());
                }
            }
            let _ = ws.close(None).await;
        });

        let cfg = SamsungConfig {
            host: "127.0.0.1".into(),
            port,
            secure_port: port,
            token: Some("pair-1".into()),
            name: "Zappe".into(),
            disabled: false,
        };
        let prev = std::env::var_os("ZAPPE_SAMSUNG_TIMEOUT_MS");
        std::env::set_var("ZAPPE_SAMSUNG_TIMEOUT_MS", "2000");
        let token = send_key(&cfg, TvKey::VolumeUp).await.expect("send_key");
        assert_eq!(token.as_deref(), Some("pair-1"));
        server.await.unwrap();
        assert_eq!(*received.lock().unwrap(), vec!["KEY_VOLUP".to_string()]);
        restore_env("ZAPPE_SAMSUNG_TIMEOUT_MS", prev);
    }

    #[tokio::test]
    async fn probe_http_reads_api_v2() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 256];
            let _ = stream.read(&mut buf).await;
            let body = r#"{"name":"[TV] Living","device":{"modelName":"UN55NU7100","TokenAuthSupport":"true"}}"#;
            let resp = format!(
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
                body.len()
            );
            let _ = stream.write_all(resp.as_bytes()).await;
        });
        let cfg = SamsungConfig {
            host: "127.0.0.1".into(),
            port,
            secure_port: 8002,
            token: None,
            name: "Zappe".into(),
            disabled: false,
        };
        let info = probe_http(&cfg).await.expect("probe");
        assert_eq!(info.model, "UN55NU7100");
        assert!(info.token_auth);
    }

    #[tokio::test]
    async fn mock_tv_unauthorized_is_pairing_error() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut ws = tokio_tungstenite::accept_async(stream).await.unwrap();
            ws.send(Message::Text(
                r#"{"event":"ms.channel.unauthorized"}"#.into(),
            ))
            .await
            .unwrap();
            let _ = ws.close(None).await;
        });
        let cfg = SamsungConfig {
            host: "127.0.0.1".into(),
            port,
            secure_port: 9,
            token: None,
            name: "Zappe".into(),
            disabled: false,
        };
        let prev = std::env::var_os("ZAPPE_SAMSUNG_TIMEOUT_MS");
        std::env::set_var("ZAPPE_SAMSUNG_TIMEOUT_MS", "2000");
        let err = send_key(&cfg, TvKey::Mute).await.unwrap_err();
        assert_eq!(err, TvError::Unauthorized);
        restore_env("ZAPPE_SAMSUNG_TIMEOUT_MS", prev);
    }
}
