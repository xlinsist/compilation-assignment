use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "assign2-checker", about = "Checker for llvm assignment 2.")]
pub struct Opt {
    /// Only check the format of output file
    #[structopt(short = "f", long = "format-only")]
    pub format_only: bool,

    /// Standard answer file
    #[structopt(long = "std", parse(from_os_str))]
    pub standard: Option<PathBuf>,

    /// Whether to apply strict format check
    #[structopt(short = "s", long = "strict")]
    pub strict: bool,

    /// Your output file
    #[structopt(parse(from_os_str))]
    pub file: PathBuf,
}
