use cxx_qt_build::{CxxQtBuilder, QmlModule};

fn main() {
    CxxQtBuilder::new()
        .qml_module(QmlModule {
            uri: "com.zappe.app",
            version_major: 1,
            version_minor: 0,
            rust_files: &["src/qt/zappe_backend.rs"],
            qml_files: &["qml/main.qml"],
            qrc_files: &[],
        })
        .qt_module("Quick")
        .qt_module("QuickControls2")
        .build();
}
