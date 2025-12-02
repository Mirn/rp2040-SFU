use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=bin2page_decoder.exe");
    println!("cargo:rerun-if-changed=decoder/bin2page_decoder.c");
    println!("cargo:rerun-if-changed=decoder/bin2page_decoder_main.c");

    let status = Command::new("gcc")
        .args(&["decoder/bin2page_decoder.c","decoder/bin2page_decoder_main.c", "-o", "target/release/bin2page_decoder"])
        .status()
        .expect("failed to compile C tool");

    assert!(status.success(), "C tool compilation failed");
}