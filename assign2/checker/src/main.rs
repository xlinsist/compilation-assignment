mod check;
mod error_code;
mod line_names;
mod opt;
mod parse;

use std::process::exit;

use check::check;
use opt::Opt;
use structopt::StructOpt;

use crate::{error_code::ErrorCode, parse::parse_file};

fn main() {
    let opt = Opt::from_args();
    if !opt.format_only && opt.standard.is_none() {
        eprintln!("Neither format-only flag nor standard file is given!");
        exit(-1);
    }
    if opt.format_only && opt.standard.is_some() {
        eprintln!("Warning: standard file is ignored due to format-only flag.");
    }

    let yours;
    match parse_file(&opt.file, opt.strict) {
        Ok(v) => {
            yours = v;
        }
        Err(e) => {
            println!("Input file format error\n{e}");
            exit(ErrorCode::InputFormatError as i32);
        }
    }
    if opt.format_only {
        return;
    }
    let standard;
    match parse_file(&opt.standard.unwrap(), opt.strict) {
        Ok(v) => {
            standard = v;
        }
        Err(e) => {
            println!("Answer file format error\n{e}");
            exit(ErrorCode::AnswerFormatError as i32);
        }
    }

    if let Err(code) = check(&yours, &standard) {
        exit(code as i32);
    }
    println!("Correct");
}
