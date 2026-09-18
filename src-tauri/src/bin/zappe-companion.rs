//! LAN phone companion (`http://zappe-tv.local`).
//!
//! Device code + QR on the TV; Netflix / Google sign-in on the phone.
//! Hidden Chrome writes the session into `~/.local/share/zappe/chrome-profile`.

use zappe_lib::run_companion;

fn main() {
    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .expect("tokio runtime");
    if let Err(err) = rt.block_on(run_companion()) {
        eprintln!("zappe-companion: {err:#}");
        std::process::exit(1);
    }
}
