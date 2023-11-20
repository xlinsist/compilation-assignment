use crate::line_names::LineNames;
use std::{fmt, path::PathBuf};

pub struct ParseError {
    index: usize,
    line: String,
    error: String,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Line {}: '{}'\n{}", self.index, self.line, self.error)
    }
}

pub fn parse_file(file: &PathBuf, strict: bool) -> Result<Vec<LineNames>, ParseError> {
    let mut result = Vec::<LineNames>::new();
    for (index, line) in std::fs::read_to_string(file).unwrap().lines().enumerate() {
        match LineNames::parse_from_str(line, strict) {
            Ok(lineinfo) => {
                result.push(lineinfo);
            }
            Err(error) => {
                Err(ParseError {
                    index: index + 1,
                    line: line.to_string(),
                    error,
                })?;
            }
        }
    }
    Ok(result)
}
