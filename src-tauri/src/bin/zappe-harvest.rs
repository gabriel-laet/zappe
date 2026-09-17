//! Dev CLI: start nest → run a versioned skill → print catalog rows.
//!
//!   cargo run --bin zappe-harvest -- --skill netflix.continue_watching.v1
//!   cargo run --bin zappe-harvest -- --fixture ../fixtures/a11y/netflix.continue_watching.sample.json

use std::path::PathBuf;

use zappe_lib::harvest_cli;
use zappe_lib::HarvestRequest;

fn main() {
    let _ = env_logger::try_init();
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut req = HarvestRequest::for_skill("netflix.continue_watching.v1");
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--skill" => {
                i += 1;
                req.skill_id = args
                    .get(i)
                    .cloned()
                    .expect("--skill needs an id");
            }
            "--fixture" => {
                i += 1;
                req.fixture = Some(PathBuf::from(args.get(i).expect("--fixture needs a path")));
                req.skip_nest = true;
            }
            "--dump" => {
                i += 1;
                req.dump = Some(PathBuf::from(args.get(i).expect("--dump needs a path")));
            }
            "--no-nest" => req.skip_nest = true,
            "--help" | "-h" => {
                print_help();
                return;
            }
            other => {
                eprintln!("unknown arg: {other}");
                print_help();
                std::process::exit(2);
            }
        }
        i += 1;
    }

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .expect("tokio runtime");
    match rt.block_on(harvest_cli(req)) {
        Ok(out) => {
            println!(
                "skill={} status={:?} rows={} teach={} {}",
                out.skill_id, out.status, out.rows, out.teach, out.message
            );
            if matches!(
                out.status,
                zappe_lib::ShelfStatus::Error
                    | zappe_lib::ShelfStatus::Stale
                    | zappe_lib::ShelfStatus::Teach
            ) {
                std::process::exit(1);
            }
        }
        Err(err) => {
            eprintln!("harvest failed: {err:#}");
            std::process::exit(1);
        }
    }
}

fn print_help() {
    eprintln!(
        "zappe-harvest — Netflix Continue Watching harvest (AT-SPI, no CDP)\n\n\
         Options:\n\
           --skill ID       default netflix.continue_watching.v1\n\
           --fixture PATH   use a saved a11y dump (skips nest)\n\
           --dump PATH      write the a11y tree JSON\n\
           --no-nest        dump AT-SPI only (Chrome already running)\n"
    );
}
