use std::env;
use std::fs;
use std::path::Path;
use std::io;
use rand::Rng;
use std::process::Command;

fn rng_avrg(max:u32, nturn:usize) -> u32 {
    let mut sum: u64 = 0;
    for _ in 0..nturn {
        sum += rand::rng().random_range(0..max) as u64;
    }
    let val = ((sum + (nturn/2) as u64) / (nturn as u64)) as u32;
    assert!(val < max);
    return val;
}

fn eq_with_trailing_ff(a: &[u8], b: &[u8]) -> bool {
    if b.len() < a.len() {
        return false;
    }
    if &b[..a.len()] != a {
        return false;
    }
    b[a.len()..].iter().all(|&byte| byte == 0xFF)
}

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        println!("Usage: bin2page_encoder input_page_A.bin input_page_B.bin output.bin");
        return Ok(());
    }

    // let mut bin_a = vec![];
    // for _ in 0 .. 1_000_000_000 {
    //     bin_a.push(rand::rng().random_range(0..256) as u8);
    // }
    // let mut bin_b = bin_a.clone();
    // let primary = rand::rng().random_range(0..255);
    // for _ in 0.. 100_000_000 {
    //     let idx = rng_avrg(bin_a.len() as u32, 2) as usize;
    //     if rand::rng().random_range(0..1000) > 10 {
    //         bin_b[idx] = bin_b[idx].wrapping_add(primary);
    //     } else {
    //         bin_b[idx] = bin_b[idx].wrapping_add(rng_avrg(256, 16) as u8);
    //     }
    // }
    // fs::write("bin_a.bin", &bin_a)?;
    // fs::write("bin_b.bin", &bin_b)?;

    let bin_a: Vec<u8> = fs::read(&args[1])?;
    let bin_b: Vec<u8> = fs::read(&args[2])?;

    if bin_a.len() != bin_b.len() {
        println!("Error: The binary sizes differ. Both binaries must be the same size.");
        return Ok(());
    }
    // let mut hyst = [0; 256];
    // let mut max_val = 0;
    // let mut max_pos = 0usize;
    // for i in 0..bin_a.len() {
    //     if bin_a[i] != bin_b[i] {
    //         let pos = bin_b[i].wrapping_sub(bin_a[i]) as usize;
    //         hyst[pos] += 1;
    //         if hyst[pos] > max_val {
    //             max_val = hyst[pos];
    //             max_pos = pos;
    //         }
    //     }
    // }
    // let primary = max_pos as u8;

    // println!("Hystogram:");
    // for i in 0..256 {
    //     if hyst[i] != 0 {
    //         println!("[{:02X}] = {}{}", i, hyst[i], if i as u8 ==primary {" <<<<<< primary"} else {""});
    //     }
    // }

    let mut res_bin = vec![b'B',b'I',b'N',b'2',b'P',b'a',b'g',b'e'];
    // res_bin.push(max_pos as u8);
    //res_bin.clear();
    //let initial_len = res_bin.len();

    let mut addr:Vec<u8> = vec![];
    let mut data:Vec<u8> = vec![];
    let mut change:Vec<u8> = vec![];
    // let mut non_primary = 0;
    let mut stop = false;
    let mut pos = 0;

    //let mut stat_small = 0;
    // let mut stat_full = 0;
    let mut stat_padded = 0;

    while !stop {
        let a = *bin_a.get(pos).unwrap_or(&0xFF);
        let b = *bin_b.get(pos).unwrap_or(&0xFF);
        pos += 1;
        let diff = b.wrapping_sub(a);

        data.push(a);
        if diff != 0 {
            addr.push((data.len()-1) as u8);
            change.push(b);
            // if diff != primary {
            //     non_primary += 1;
            // }
        }
        // if non_primary == 0 {
        //     let block_len = 1 + addr.len() + change.len() + data.len();
        //     if block_len >= 254 {
        //         let block_len = 1 + addr.len() + data.len();
        //         let next_a = *bin_a.get(pos).unwrap_or(&0xFF);
        //         let next_b = *bin_b.get(pos).unwrap_or(&0xFF);
        //         if (next_a == next_b) && (block_len < 255) {
        //             continue;
        //         }
        //         assert!(block_len <= 256);
        //         let padding = 256 - block_len;
        //         stat_padded += padding;
        //         res_bin.push((addr.len() + padding) as u8);
        //         res_bin.extend(vec![0xFF; padding as usize].iter());                 
        //         res_bin.extend(addr.iter());
        //         res_bin.extend(data.iter());

        //         if res_bin.len() < 5000 {
        //             println!("{:08X}\t{}\t{}\t{}\t{}\t{}", res_bin.len() - initial_len, "Small", addr.len(), change.len(), padding, if block_len != 256 {"Padded"} else {""});
        //         }                

        //         addr.clear();
        //         data.clear();
        //         change.clear();
        //         non_primary = 0;
        //         stop = (pos >= bin_a.len()) && (pos >= bin_b.len());
        //         stat_small += 1;
        //     }
        // } else 
        {
            let block_len = 1 + addr.len() + change.len() + data.len();
            if block_len >= 254 {
                if block_len > 256 {
                    println!("block_len: {block_len}");
                }
                assert!(block_len <= 256);
                let padding = 256 - block_len;
                stat_padded += padding;
                res_bin.push(((addr.len() + padding) as u8) | 0x80); // make negative full format marker
                res_bin.extend(vec![0xFF; padding as usize].iter());
                res_bin.extend(addr.iter());
                res_bin.extend(change.iter());
                res_bin.extend(data.iter());

                // if res_bin.len() < 50000 {
                //     println!("{:08X}\t{}\t{}\t{}\t{}", res_bin.len() - initial_len, "Full", addr.len(), change.len(), if block_len == 254 {"Padded1"} else {if block_len == 255 {"Padded2"} else {""}});
                // }

                addr.clear();
                data.clear();
                change.clear();
                //non_primary = 0;
                //stat_full += 1;
                stop = (pos >= bin_a.len()) && (pos >= bin_b.len());
                // NOTE about stop:
                // We intentionally keep feeding 0xFF/0xFF after the end of both images
                // until the last block is full enough to be flushed.
                // On decode this only adds trailing 0xFF bytes, which are considered padding.
            }
        }
    }


    // println!("stat_full:   {stat_full}");
    // println!("stat_small:  {stat_small}");
    println!("stat_padded: {stat_padded}");
    println!("original size: {}", bin_a.len());
    println!("encoded size:  {}", res_bin.len());
    println!("size delta: {}\t{:2.2}%", res_bin.len() - bin_a.len(), 100.0f32 * (res_bin.len() - bin_a.len()) as f32 / bin_a.len() as f32);

    let outname = &args[3].to_string();
    fs::write(outname, res_bin)?;

    let current_exe = env::current_exe().expect("cannot get exe path");
    let mut tool_path = current_exe.parent().expect("no parent dir for current exe").to_path_buf();

    #[cfg(windows)]
    tool_path.push("bin2page_decoder.exe");

    #[cfg(not(windows))]
    tool_path.push("bin2page_decoder"); // ELF под Linux/macOS
    let tool = tool_path.to_str().unwrap();
    println!("Run tool {tool}");
    let out = Command::new(tool)
        .arg(&outname)
        .output()
        .expect("failed to execute C tool"); //TODO: add stderr & exitcode checking
    println!("stdout:\n{}", String::from_utf8_lossy(&out.stdout));

    let fn_dec_a = format!("{outname}.bin_A");
    let fn_dec_b = format!("{outname}.bin_B");
    let decoded_a: Vec<u8> = fs::read(&fn_dec_a)?;
    let decoded_b: Vec<u8> = fs::read(&fn_dec_b)?;
    let _ = fs::remove_file(Path::new(&fn_dec_a))?;
    let _ = fs::remove_file(Path::new(&fn_dec_b))?;

    if eq_with_trailing_ff(&bin_a, &decoded_a) {println!("Variant A OK");} else {println!("Variant A ERROR");};
    if eq_with_trailing_ff(&bin_b, &decoded_b) {println!("Variant B OK");} else {println!("Variant B ERROR");};

    return Ok(())
}
