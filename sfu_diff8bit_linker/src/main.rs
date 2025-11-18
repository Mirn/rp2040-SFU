use std::fs;
use std::io;

fn main() -> io::Result<()> {
    let bin_a: Vec<u8> = fs::read("C:\\Users\\sj21d\\Documents\\Pico-v1.5.1\\test\\payspot-tester\\build\\payspot_tester_a.bin")?;
    let bin_b: Vec<u8> = fs::read("C:\\Users\\sj21d\\Documents\\Pico-v1.5.1\\test\\payspot-tester\\build\\payspot_tester_b.bin")?;
    if bin_a.len() != bin_b.len() {
        println!("Error: The binary sizes differ. Both binaries must be the same size.");
        return Ok(());
    }
    let mut hyst = [0; 256];
    let mut max_val = 0;
    let mut max_pos = 0usize;
    for i in 0..bin_a.len() {
        if bin_a[i] != bin_b[i] {
            let pos = bin_b[i].wrapping_sub(bin_a[i]) as usize;
            hyst[pos] += 1;
            if hyst[pos] > max_val {
                max_val = hyst[pos];
                max_pos = pos;
            }
        }
    }
    let primary = max_pos as u8;

    println!("Hystogram:");
    for i in 0..256 {
        if hyst[i] != 0 {
            println!("[{:02X}] = {}{}", i, hyst[i], if i as u8 ==primary {" <<<<<< primary"} else {""});
        }
    }

    let mut res_bin = vec![b'D',b'I',b'F',b'F',b'2',b'0',b'4',b'0'];
    res_bin.push(max_pos as u8);
    let initial_len = res_bin.len();

    let mut addr:Vec<u8> = vec![];
    let mut data:Vec<u8> = vec![];
    let mut change:Vec<u8> = vec![];
    let mut non_primary = 0;
    let mut stop = false;
    let mut pos = 0;

    while !stop {        
        let a = if pos < bin_a.len() {bin_a[pos]} else {0xFFu8};
        let b = if pos < bin_b.len() {bin_b[pos]} else {0xFFu8};
        pos += 1;
        let diff = b.wrapping_sub(a);

        data.push(a);
        if diff != 0 {
            addr.push((data.len()-1) as u8);
            change.push(b);
            if diff != primary {
                non_primary += 1;
            }
        }
        if non_primary == 0 {
            let block_len = 1 + addr.len() + data.len();
            if block_len >= 255 {
                let padding = 256 - block_len;
                res_bin.push((addr.len() + padding) as u8);
                res_bin.extend(vec![0xFF; padding as usize].iter());
                res_bin.extend(addr.iter());
                res_bin.extend(data.iter());

                println!("{:08X}\t{}\t{}\t{}\t{}", res_bin.len() - initial_len, "Small", addr.len(), change.len(), if block_len != 256 {"Padded"} else {""});

                addr.clear();
                data.clear();
                change.clear();
                non_primary = 0;
                stop = (pos >= bin_a.len()) && (pos >= bin_b.len());
            }
        } else {
            let block_len = 1 + addr.len() + change.len() + data.len();
            if block_len >= 254 {
                let padding = 256 - block_len;
                res_bin.push(((addr.len() + padding) as u8) | 0x80); // make negative full format marker
                res_bin.extend(vec![0xFF; padding as usize].iter());
                res_bin.extend(addr.iter());
                res_bin.extend(change.iter());
                res_bin.extend(data.iter());

                println!("{:08X}\t{}\t{}\t{}\t{}", res_bin.len() - initial_len, "Full", addr.len(), change.len(), if block_len == 254 {"Padded1"} else {if block_len == 255 {"Padded2"} else {""}});

                addr.clear();
                data.clear();
                change.clear();
                non_primary = 0;
                stop = (pos >= bin_a.len()) && (pos >= bin_b.len());
            }
        }
    }

    fs::write("test.bin2040diff", res_bin)?;
    return Ok(())
}
