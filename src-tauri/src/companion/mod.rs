//! LAN phone companion: device code + QR on the TV, Netflix login on the phone.
//!
//! The TV never asks for a password. Chrome may run hidden (no `-f`, hide / Xvfb)
//! so HDMI stays on the Zappe Connect screen.

use std::path::Path;
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use anyhow::{anyhow, Context, Result};
use axum::extract::{Path as AxumPath, Query, State};
use axum::http::{header, HeaderValue, Method, Request, StatusCode};
use axum::middleware::{self, Next};
use axum::response::{Html, IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use qrcode::render::svg;
use qrcode::QrCode;
use serde::{Deserialize, Serialize};
use tokio::net::TcpListener;
use tokio::sync::Mutex;

mod login;
pub mod sources;

use login::{LoginJob, LoginProvider};
use sources::{AuthSource, SourceView};
use crate::nest::NestManager;
use crate::paths::{companion_session_path, ensure_data_dir};
use crate::setup::{load_setup, save_setup};

const PHONE_HTML: &str = include_str!("phone.html");
const CODE_ALPHABET: &[u8] = b"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const CODE_LEN: usize = 8;
const SESSION_TTL_SECS: u64 = 15 * 60;
pub const DEFAULT_HOST: &str = "zappe-tv.local";
pub const FALLBACK_PORT: u16 = 8780;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SessionStatus {
    Waiting,
    PhoneOpen,
    SigningIn,
    NeedsFactor,
    Connected,
    Error,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SessionView {
    pub code: String,
    pub display_code: String,
    pub status: SessionStatus,
    pub message: String,
    pub public_url: String,
    pub claim_url: String,
    pub qr_svg: String,
    pub host: String,
    pub port: u16,
    pub expires_in_secs: u64,
    pub nest_hidden: bool,
    pub accounts_connected: bool,
    pub source: String,
    pub source_label: String,
    pub source_wired: bool,
    pub harvest_skill: Option<String>,
    pub sources: Vec<SourceView>,
}

#[derive(Clone)]
pub struct Hub {
    inner: Arc<Mutex<Inner>>,
    nest: NestManager,
}

struct Inner {
    session: Session,
    public_host: String,
    port: u16,
    /// Existing systemd / leftover companion. TV polls this so codes match.
    remote_base: Option<String>,
}

#[derive(Clone, Debug)]
struct Session {
    code: String,
    created_unix: u64,
    expires_unix: u64,
    status: SessionStatus,
    message: String,
    paired: bool,
    pending_factor: Option<String>,
    source: String,
}

#[derive(Deserialize)]
struct ClaimBody {
    code: String,
}

#[derive(Deserialize)]
struct LoginBody {
    code: String,
    #[serde(default)]
    email: String,
    #[serde(default)]
    password: String,
    #[serde(default)]
    provider: Option<String>,
    #[serde(default)]
    source: Option<String>,
}

#[derive(Deserialize)]
struct SourceBody {
    source: String,
}

#[derive(Deserialize)]
struct FactorBody {
    code: String,
    factor: String,
}

#[derive(Deserialize)]
struct StatusQuery {
    code: Option<String>,
}

impl Hub {
    pub fn new(nest: NestManager) -> Self {
        let now = unix_now();
        Self {
            inner: Arc::new(Mutex::new(Inner {
                session: fresh_session(now),
                public_host: public_host(),
                port: FALLBACK_PORT,
                remote_base: None,
            })),
            nest,
        }
    }

    pub async fn set_listen_port(&self, port: u16) {
        self.inner.lock().await.port = port;
    }

    pub async fn view(&self) -> SessionView {
        if let Some(base) = self.remote_base().await {
            if let Ok(view) = remote_view(&base, false).await {
                return view;
            }
        }
        let mut inner = self.inner.lock().await;
        refresh_locked(&mut inner);
        persist_locked(&inner);
        view_locked(&inner)
    }

    pub async fn begin(&self) -> SessionView {
        if let Some(base) = self.remote_base().await {
            if let Ok(view) = remote_view(&base, true).await {
                return view;
            }
        }
        let mut inner = self.inner.lock().await;
        inner.session = fresh_session(unix_now());
        refresh_locked(&mut inner);
        persist_locked(&inner);
        view_locked(&inner)
    }

    async fn remote_base(&self) -> Option<String> {
        self.inner.lock().await.remote_base.clone()
    }

    pub async fn select_source(&self, raw: &str) -> SessionView {
        let source = sources::require(raw);
        let mut inner = self.inner.lock().await;
        inner.session.source = source.id.to_string();
        if inner.session.status != SessionStatus::SigningIn
            && inner.session.status != SessionStatus::NeedsFactor
        {
            inner.session.status = if sources::is_connected(source.id) {
                SessionStatus::Connected
            } else if inner.session.paired {
                SessionStatus::PhoneOpen
            } else {
                SessionStatus::Waiting
            };
            inner.session.message = source_wait_message(source);
        }
        persist_locked(&inner);
        view_locked(&inner)
    }

    pub async fn claim(&self, raw: &str) -> Result<SessionView> {
        let mut inner = self.inner.lock().await;
        refresh_locked(&mut inner);
        if !codes_match(&inner.session.code, raw) {
            return Err(anyhow!("that code does not match the TV"));
        }
        if inner.session.status == SessionStatus::Connected {
            return Ok(view_locked(&inner));
        }
        inner.session.paired = true;
        if inner.session.status == SessionStatus::Waiting {
            inner.session.status = SessionStatus::PhoneOpen;
            let src = current_source(&inner);
            inner.session.message = format!(
                "Phone connected. Sign in to {} here — the TV stays on Connect.",
                src.label
            );
        }
        persist_locked(&inner);
        Ok(view_locked(&inner))
    }

    pub async fn start_login(&self, body: LoginBody) -> Result<SessionView> {
        {
            let mut inner = self.inner.lock().await;
            refresh_locked(&mut inner);
            if !codes_match(&inner.session.code, &body.code) {
                return Err(anyhow!("that code does not match the TV"));
            }
            if inner.session.status == SessionStatus::Connected {
                return Ok(view_locked(&inner));
            }
            inner.session.paired = true;
            inner.session.status = SessionStatus::SigningIn;
            inner.session.message =
                "Opening a hidden Chrome with this TV's profile. HDMI stays on Zappe.".into();
            persist_locked(&inner);
        }

        if let Some(src) = body.source.as_deref() {
            let _ = self.select_source(src).await;
        }
        let source = {
            let inner = self.inner.lock().await;
            current_source(&inner).id.to_string()
        };
        let provider = LoginProvider::parse(body.provider.as_deref());
        let job = LoginJob {
            email: body.email.trim().to_string(),
            password: body.password,
            provider,
            source,
        };
        if job.email.is_empty() {
            self.set_error("Email is required on the phone.").await;
            return Err(anyhow!("email is required"));
        }

        if crate::nest::env_flag("ZAPPE_LOGIN_FAKE") {
            sources::mark_connected(&job.source);
            self.set_connected(format!(
                "Connected. {} is on this TV — HDMI never left Zappe.",
                sources::require(&job.source).label
            ))
            .await;
            return Ok(self.view().await);
        }

        let hub = self.clone();
        let nest = self.nest.clone();
        tokio::spawn(async move {
            login::drive_login(hub, nest, job).await;
        });
        Ok(self.view().await)
    }

    pub async fn submit_factor(&self, raw_code: &str, factor: String) -> Result<SessionView> {
        let mut inner = self.inner.lock().await;
        refresh_locked(&mut inner);
        if !codes_match(&inner.session.code, raw_code) {
            return Err(anyhow!("that code does not match the TV"));
        }
        let factor = factor.trim().to_string();
        if factor.is_empty() {
            return Err(anyhow!("enter the code from email or 2FA"));
        }
        inner.session.pending_factor = Some(factor);
        inner.session.status = SessionStatus::SigningIn;
        inner.session.message = "Sending the extra code into the hidden browser…".into();
        persist_locked(&inner);
        Ok(view_locked(&inner))
    }

    pub async fn take_factor(&self) -> Option<String> {
        let mut inner = self.inner.lock().await;
        inner.session.pending_factor.take()
    }

    pub async fn set_status(&self, status: SessionStatus, message: impl Into<String>) {
        let mut inner = self.inner.lock().await;
        inner.session.status = status.clone();
        inner.session.message = message.into();
        if status == SessionStatus::Connected {
            sources::mark_connected(&inner.session.source);
            mark_accounts_done();
        }
        persist_locked(&inner);
    }

    pub async fn set_error(&self, message: impl Into<String>) {
        self.set_status(SessionStatus::Error, message).await;
    }

    pub async fn set_connected(&self, message: impl Into<String>) {
        self.set_status(SessionStatus::Connected, message).await;
    }
}

fn fresh_session(now: u64) -> Session {
    Session {
        code: mint_code(),
        created_unix: now,
        expires_unix: now.saturating_add(SESSION_TTL_SECS),
        status: SessionStatus::Waiting,
        message: "Open this on your phone. The TV will never ask for a password.".into(),
        paired: false,
        pending_factor: None,
        source: sources::default_source().id.to_string(),
    }
}

fn refresh_locked(inner: &mut Inner) {
    let now = unix_now();
    if inner.session.status != SessionStatus::Connected
        && now >= inner.session.expires_unix
    {
        inner.session = fresh_session(now);
    }
    let src = current_source(inner);
    if inner.session.status != SessionStatus::Connected && sources::is_connected(src.id)
    {
        inner.session.status = SessionStatus::Connected;
        inner.session.message = format!(
            "{} is already on this TV. You can put the phone down.",
            src.label
        );
        sources::mark_connected(src.id);
        mark_accounts_done();
    }
}

fn view_locked(inner: &Inner) -> SessionView {
    let now = unix_now();
    let public_url = public_base(&inner.public_host, inner.port);
    let src = current_source(inner);
    let claim_url = format!(
        "{}/c/{}?s={}",
        public_url.trim_end_matches('/'),
        inner.session.code,
        src.id
    );
    let source_list = sources::source_views();
    let selected_connected = source_list.iter().any(|s| s.id == src.id && s.connected);
    SessionView {
        display_code: display_code(&inner.session.code),
        qr_svg: qr_svg(&claim_url),
        code: inner.session.code.clone(),
        status: inner.session.status.clone(),
        message: inner.session.message.clone(),
        public_url,
        claim_url,
        host: inner.public_host.clone(),
        port: inner.port,
        expires_in_secs: inner.session.expires_unix.saturating_sub(now),
        nest_hidden: true,
        accounts_connected: selected_connected,
        source: src.id.to_string(),
        source_label: src.label.to_string(),
        source_wired: src.wired,
        harvest_skill: src.harvest_skill.map(|s| s.to_string()),
        sources: source_list,
    }
}

fn persist_locked(inner: &Inner) {
    let view = view_locked(inner);
    if let Err(err) = write_session_file(&view) {
        log::warn!("companion session persist: {err:#}");
    }
}

fn write_session_file(view: &SessionView) -> Result<()> {
    ensure_data_dir()?;
    let path = companion_session_path();
    let json = serde_json::json!({
        "code": view.code,
        "display_code": view.display_code,
        "status": view.status,
        "message": view.message,
        "public_url": view.public_url,
        "claim_url": view.claim_url,
        "source": view.source,
        "sources": view.sources,
        "updated_at": unix_now(),
    });
    std::fs::write(path, serde_json::to_vec_pretty(&json)?)?;
    Ok(())
}

fn current_source(inner: &Inner) -> &'static AuthSource {
    sources::require(&inner.session.source)
}

fn source_wait_message(source: &AuthSource) -> String {
    if source.wired {
        format!(
            "Open this on your phone to sign in to {}. The TV will never ask for a password.",
            source.label
        )
    } else {
        format!(
            "Sign in to {} on your phone. Same hidden Chrome — harvest for this source is a follow-up skill.",
            source.label
        )
    }
}

fn mark_accounts_done() {
    let mut setup = load_setup();
    if setup.accounts_done {
        return;
    }
    setup.accounts_done = true;
    if let Err(err) = save_setup(&setup) {
        log::warn!("setup accounts_done: {err:#}");
    }
}

pub fn mint_code() -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let mut hasher = DefaultHasher::new();
    unix_now().hash(&mut hasher);
    std::process::id().hash(&mut hasher);
    SystemTime::now().hash(&mut hasher);
    let mut n = hasher.finish();
    // Mix in a few bytes from /dev/urandom when present.
    if let Ok(mut file) = std::fs::File::open("/dev/urandom") {
        use std::io::Read;
        let mut bytes = [0u8; 8];
        if file.read_exact(&mut bytes).is_ok() {
            for (i, b) in bytes.iter().enumerate() {
                n ^= (*b as u64) << (i * 8);
            }
        }
    }
    let mut out = String::with_capacity(CODE_LEN);
    for _ in 0..CODE_LEN {
        out.push(CODE_ALPHABET[(n as usize) % CODE_ALPHABET.len()] as char);
        n = n.rotate_left(5).wrapping_mul(16777619);
    }
    out
}

pub fn display_code(code: &str) -> String {
    let n = normalize_code(code);
    if n.len() == CODE_LEN {
        format!("{}-{}", &n[..4], &n[4..])
    } else {
        n
    }
}

pub fn normalize_code(raw: &str) -> String {
    raw.chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_uppercase())
        .collect()
}

pub fn codes_match(expected: &str, raw: &str) -> bool {
    normalize_code(expected) == normalize_code(raw)
}

pub fn public_host() -> String {
    std::env::var("ZAPPE_COMPANION_HOST")
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| DEFAULT_HOST.to_string())
}

pub fn public_base(host: &str, port: u16) -> String {
    if port == 80 {
        format!("http://{host}")
    } else {
        format!("http://{host}:{port}")
    }
}

pub fn preferred_ports() -> Vec<u16> {
    if let Ok(raw) = std::env::var("ZAPPE_COMPANION_PORT") {
        if let Ok(port) = raw.parse::<u16>() {
            return vec![port];
        }
    }
    vec![80, FALLBACK_PORT]
}

pub fn qr_svg(url: &str) -> String {
    match QrCode::new(url.as_bytes()) {
        Ok(code) => code
            .render::<svg::Color<'_>>()
            .min_dimensions(220, 220)
            .dark_color(svg::Color("#111111"))
            .light_color(svg::Color("#ffffff"))
            .quiet_zone(true)
            .build(),
        Err(_) => String::new(),
    }
}

pub fn profile_has_netflix_session(profile: &Path) -> bool {
    const NEEDLE: &[u8] = b"NetflixId";
    for rel in [
        "Default/Network/Cookies",
        "Default/Cookies",
        "Default/Network/Cookies-wal",
        "Default/Cookies-wal",
    ] {
        let path = profile.join(rel);
        if file_contains(&path, NEEDLE) {
            return true;
        }
        // Chrome locks the live DB; a copy still has plaintext cookie names.
        if let Some(parent) = path.parent() {
            let tmp = parent.join("zappe-cookies-scan");
            if std::fs::copy(&path, &tmp).is_ok() && file_contains(&tmp, NEEDLE) {
                let _ = std::fs::remove_file(&tmp);
                return true;
            }
            let _ = std::fs::remove_file(&tmp);
        }
    }
    false
}

fn file_contains(path: &Path, needle: &[u8]) -> bool {
    let Ok(bytes) = std::fs::read(path) else {
        return false;
    };
    bytes.windows(needle.len()).any(|w| w == needle)
}

fn unix_now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

async fn cors(req: Request<axum::body::Body>, next: Next) -> Response {
    if req.method() == Method::OPTIONS {
        let mut res = StatusCode::NO_CONTENT.into_response();
        add_cors(res.headers_mut());
        return res;
    }
    let mut res = next.run(req).await;
    add_cors(res.headers_mut());
    res
}

fn add_cors(headers: &mut axum::http::HeaderMap) {
    headers.insert(
        header::ACCESS_CONTROL_ALLOW_ORIGIN,
        HeaderValue::from_static("*"),
    );
    headers.insert(
        header::ACCESS_CONTROL_ALLOW_METHODS,
        HeaderValue::from_static("GET,POST,OPTIONS"),
    );
    headers.insert(
        header::ACCESS_CONTROL_ALLOW_HEADERS,
        HeaderValue::from_static("content-type"),
    );
}

fn router(hub: Hub) -> Router {
    Router::new()
        .route("/", get(page_home))
        .route("/c/{code}", get(page_claim))
        .route("/health", get(health))
        .route("/api/tv", get(api_tv))
        .route("/api/tv/begin", post(api_begin))
        .route("/api/tv/source", post(api_source))
        .route("/api/sources", get(api_sources))
        .route("/api/status", get(api_status))
        .route("/api/claim", post(api_claim))
        .route("/api/login", post(api_login))
        .route("/api/factor", post(api_factor))
        .layer(middleware::from_fn(cors))
        .with_state(hub)
}

async fn health() -> &'static str {
    "ok"
}

async fn page_home() -> Html<&'static str> {
    Html(PHONE_HTML)
}

async fn page_claim(AxumPath(_code): AxumPath<String>) -> Html<&'static str> {
    Html(PHONE_HTML)
}

async fn api_tv(State(hub): State<Hub>) -> Json<SessionView> {
    Json(hub.view().await)
}

async fn api_begin(State(hub): State<Hub>) -> Json<SessionView> {
    Json(hub.begin().await)
}

async fn api_source(State(hub): State<Hub>, Json(body): Json<SourceBody>) -> Json<SessionView> {
    Json(hub.select_source(&body.source).await)
}

async fn api_sources() -> Json<Vec<SourceView>> {
    Json(sources::source_views())
}

async fn api_status(
    State(hub): State<Hub>,
    Query(q): Query<StatusQuery>,
) -> Result<Json<SessionView>, (StatusCode, Json<serde_json::Value>)> {
    let view = hub.view().await;
    if let Some(code) = q.code {
        if !codes_match(&view.code, &code) && view.status != SessionStatus::Connected {
            return Err(json_err(StatusCode::FORBIDDEN, "code does not match"));
        }
    }
    Ok(Json(view))
}

async fn api_claim(
    State(hub): State<Hub>,
    Json(body): Json<ClaimBody>,
) -> Result<Json<SessionView>, (StatusCode, Json<serde_json::Value>)> {
    hub.claim(&body.code)
        .await
        .map(Json)
        .map_err(|e| json_err(StatusCode::FORBIDDEN, e.to_string()))
}

async fn api_login(
    State(hub): State<Hub>,
    Json(body): Json<LoginBody>,
) -> Result<Json<SessionView>, (StatusCode, Json<serde_json::Value>)> {
    hub.start_login(body)
        .await
        .map(Json)
        .map_err(|e| json_err(StatusCode::BAD_REQUEST, e.to_string()))
}

async fn api_factor(
    State(hub): State<Hub>,
    Json(body): Json<FactorBody>,
) -> Result<Json<SessionView>, (StatusCode, Json<serde_json::Value>)> {
    hub.submit_factor(&body.code, body.factor)
        .await
        .map(Json)
        .map_err(|e| json_err(StatusCode::BAD_REQUEST, e.to_string()))
}

fn json_err(status: StatusCode, message: impl Into<String>) -> (StatusCode, Json<serde_json::Value>) {
    (
        status,
        Json(serde_json::json!({ "error": message.into() })),
    )
}

/// Bind the first free preferred port and serve the phone companion.
pub async fn serve(hub: Hub) -> Result<u16> {
    let bind_host = std::env::var("ZAPPE_COMPANION_BIND").unwrap_or_else(|_| "0.0.0.0".into());
    let mut last_err = None;
    for port in preferred_ports() {
        let addr = format!("{bind_host}:{port}");
        match TcpListener::bind(&addr).await {
            Ok(listener) => {
                let actual = listener.local_addr().map(|a| a.port()).unwrap_or(port);
                hub.set_listen_port(actual).await;
                log::info!(
                    "phone companion on http://{} (bind {addr})",
                    public_base(&public_host(), actual)
                );
                axum::serve(listener, router(hub))
                    .await
                    .context("companion http")?;
                return Ok(actual);
            }
            Err(err) => {
                log::info!("companion bind {addr} failed: {err}");
                last_err = Some(err);
            }
        }
    }
    Err(anyhow!(
        "could not bind companion on {:?}: {:?}",
        preferred_ports(),
        last_err
    ))
}

#[allow(dead_code)]
pub async fn serve_on_listener(hub: Hub, listener: TcpListener) -> Result<()> {
    let port = listener.local_addr()?.port();
    hub.set_listen_port(port).await;
    axum::serve(listener, router(hub))
        .await
        .context("companion http")?;
    Ok(())
}

/// Probe an already-running companion (systemd unit on :80, or a previous bind).
pub async fn probe_remote() -> Option<String> {
    if std::env::var("ZAPPE_COMPANION")
        .map(|s| s == "0" || s.eq_ignore_ascii_case("remote"))
        .unwrap_or(false)
    {
        for port in preferred_ports() {
            let base = format!("http://127.0.0.1:{port}");
            if health_ok(&base).await {
                return Some(base);
            }
        }
        return None;
    }
    for port in [80u16, FALLBACK_PORT] {
        let base = format!("http://127.0.0.1:{port}");
        if health_ok(&base).await {
            return Some(base);
        }
    }
    None
}

async fn health_ok(base: &str) -> bool {
    match tokio::time::timeout(Duration::from_millis(250), http_get(&format!("{base}/health")))
        .await
    {
        Ok(Ok(body)) => body.trim() == "ok",
        _ => false,
    }
}

pub async fn remote_view(base: &str, begin: bool) -> Result<SessionView> {
    let path = if begin { "/api/tv/begin" } else { "/api/tv" };
    let method = if begin { "POST" } else { "GET" };
    let body = http_json(method, &format!("{base}{path}"), None).await?;
    serde_json::from_str(&body).context("parse companion session")
}

async fn http_get(url: &str) -> Result<String> {
    http_json("GET", url, None).await
}

async fn http_json(method: &str, url: &str, body: Option<&str>) -> Result<String> {
    let parsed = parse_http_url(url)?;
    let mut stream = tokio::net::TcpStream::connect((parsed.host.as_str(), parsed.port))
        .await
        .with_context(|| format!("connect {}", parsed.host))?;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    let mut req = format!(
        "{method} {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n",
        path = parsed.path,
        host = parsed.host,
    );
    if let Some(body) = body {
        req.push_str("Content-Type: application/json\r\n");
        req.push_str(&format!("Content-Length: {}\r\n", body.len()));
        req.push_str("\r\n");
        req.push_str(body);
    } else {
        req.push_str("\r\n");
    }
    stream.write_all(req.as_bytes()).await?;
    let mut buf = Vec::new();
    stream.read_to_end(&mut buf).await?;
    let text = String::from_utf8_lossy(&buf);
    let Some((_, rest)) = text.split_once("\r\n\r\n") else {
        return Ok(text.into_owned());
    };
    Ok(rest.to_string())
}

struct ParsedHttp {
    host: String,
    port: u16,
    path: String,
}

fn parse_http_url(url: &str) -> Result<ParsedHttp> {
    let rest = url
        .strip_prefix("http://")
        .ok_or_else(|| anyhow!("only http:// companion URLs are supported"))?;
    let (hostport, path) = match rest.split_once('/') {
        Some((h, p)) => (h, format!("/{p}")),
        None => (rest, "/".into()),
    };
    let (host, port) = match hostport.split_once(':') {
        Some((h, p)) => (h.to_string(), p.parse().unwrap_or(80)),
        None => (hostport.to_string(), 80),
    };
    Ok(ParsedHttp { host, port, path })
}

/// Serve locally, or attach to a systemd companion that already owns :80.
pub async fn boot(hub: Hub) {
    if let Some(base) = probe_remote().await {
        log::info!("using existing phone companion at {base}");
        {
            let mut inner = hub.inner.lock().await;
            inner.remote_base = Some(base.clone());
        }
        spawn_remote_poll(hub, base);
        return;
    }
    if let Err(err) = serve(hub).await {
        log::warn!("companion server stopped: {err:#}");
    }
}

fn spawn_remote_poll(hub: Hub, base: String) {
    tokio::spawn(async move {
        loop {
            tokio::time::sleep(Duration::from_millis(1500)).await;
            if let Ok(view) = remote_view(&base, false).await {
                let mut inner = hub.inner.lock().await;
                inner.port = view.port;
                inner.public_host = view.host;
                inner.session.code = view.code;
                inner.session.status = view.status;
                inner.session.message = view.message;
                inner.session.source = view.source;
                persist_locked(&inner);
            }
        }
    });
}

pub async fn run_standalone() -> Result<()> {
    let _ = env_logger::try_init();
    let nest = NestManager::start();
    let hub = Hub::new(nest);
    serve(hub).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn device_code_is_tv_friendly() {
        let code = mint_code();
        assert_eq!(code.len(), 8);
        assert!(code.chars().all(|c| CODE_ALPHABET.contains(&(c as u8))));
        let shown = display_code(&code);
        assert_eq!(shown.len(), 9);
        assert_eq!(shown.chars().nth(4), Some('-'));
        assert!(codes_match(&code, &shown.to_ascii_lowercase()));
        assert!(codes_match(&code, &format!("{} {}", &code[..4], &code[4..])));
        assert!(!codes_match(&code, "ZZZZ-ZZZZ"));
    }

    #[test]
    fn public_url_omits_port_80() {
        assert_eq!(public_base("zappe-tv.local", 80), "http://zappe-tv.local");
        assert_eq!(
            public_base("zappe-tv.local", 8780),
            "http://zappe-tv.local:8780"
        );
    }

    #[test]
    fn qr_svg_encodes_claim_url() {
        let svg = qr_svg("http://zappe-tv.local/c/W7K2MQ4P");
        assert!(svg.contains("<svg"));
        assert!(svg.contains("viewBox") || svg.contains("<path") || svg.contains("<rect"));
    }

    #[test]
    fn netflix_cookie_scan_looks_for_session_id() {
        let dir = PathBuf::from("/tmp/zappe-companion-cookie-test");
        let cookies = dir.join("Default/Network/Cookies");
        std::fs::create_dir_all(cookies.parent().unwrap()).unwrap();
        std::fs::write(&cookies, b"xxxxNetflixIdyyyy.netflix.com").unwrap();
        assert!(profile_has_netflix_session(&dir));
        std::fs::write(&cookies, b"only nfvdid and flwssn").unwrap();
        assert!(!profile_has_netflix_session(&dir));
        let _ = std::fs::remove_dir_all(dir);
    }

    #[tokio::test]
    async fn companion_http_pairs_phone_and_fakes_login() {
        let prev_fake = std::env::var_os("ZAPPE_LOGIN_FAKE");
        let prev_dir = std::env::var_os("ZAPPE_DATA_DIR");
        std::env::set_var("ZAPPE_LOGIN_FAKE", "1");
        let dir = std::env::temp_dir().join(format!(
            "zappe-companion-http-{}-{}",
            std::process::id(),
            unix_now()
        ));
        let _ = std::fs::remove_dir_all(&dir);
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        let nest = NestManager::start();
        let hub = Hub::new(nest);
        let view = hub.view().await;
        assert_eq!(view.status, SessionStatus::Waiting);
        assert_eq!(view.source, "netflix");
        assert!(view.sources.iter().any(|s| s.id == "prime" && !s.wired));
        assert!(!view.display_code.contains("coming soon"));
        let prime = hub.select_source("prime").await;
        assert_eq!(prime.source, "prime");
        assert_eq!(prime.source_label, "Prime Video");
        hub.select_source("netflix").await;
        hub.claim(&view.display_code).await.unwrap();
        let after = hub
            .start_login(LoginBody {
                code: view.code.clone(),
                email: "couch@example.com".into(),
                password: "secret".into(),
                provider: Some("password".into()),
                source: Some("netflix".into()),
            })
            .await
            .unwrap();
        assert!(matches!(
            after.status,
            SessionStatus::SigningIn | SessionStatus::Connected
        ));
        tokio::time::sleep(Duration::from_millis(80)).await;
        let done = hub.view().await;
        assert_eq!(done.status, SessionStatus::Connected);
        match prev_fake {
            Some(v) => std::env::set_var("ZAPPE_LOGIN_FAKE", v),
            None => std::env::remove_var("ZAPPE_LOGIN_FAKE"),
        }
        match prev_dir {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn phone_page_is_not_a_stub() {
        assert!(PHONE_HTML.contains("Sign in on this phone"));
        assert!(!PHONE_HTML.to_ascii_lowercase().contains("coming soon"));
        assert!(PHONE_HTML.contains("/api/login"));
    }

    #[test]
    fn tv_connect_is_not_a_coming_soon_stub() {
        let phone = include_str!("../../../src/components/ConnectPhone.tsx");
        let setup = include_str!("../../../src/components/SetupWizard.tsx");
        for src in [phone, setup] {
            let lower = src.to_ascii_lowercase();
            assert!(!lower.contains("coming soon"));
            assert!(!lower.contains("follow-up"));
            assert!(!lower.contains("when the phone companion ships"));
        }
        assert!(phone.contains("ConnectVisual"));
        assert!(setup.contains("ConnectVisual"));
        assert!(phone.contains("source") || setup.contains("source"));
    }
}
