use std::{fmt, str::FromStr};

pub struct LineNames {
    pub number: usize,
    pub names: Vec<String>,
}

impl PartialEq for LineNames {
    fn eq(&self, other: &Self) -> bool {
        self.number.eq(&other.number) && {
            let (mut names1, mut names2) = (self.names.clone(), other.names.clone());
            names1.sort();
            names2.sort();
            names1.eq(&names2)
        }
    }
}

impl Eq for LineNames {}

impl fmt::Display for LineNames {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} : {}",
            self.number,
            self.names
                .iter()
                .map(|s| s.to_owned())
                .reduce(|s, s2| s + ", " + &s2)
                .unwrap_or_default()
        )
    }
}

impl LineNames {
    pub fn new(number: usize) -> Self {
        Self {
            number,
            names: Vec::new(),
        }
    }

    pub fn add_name(&mut self, name: &str) {
        self.names.push(name.to_string());
    }

    pub fn parse_from_str(s: &str, strict: bool) -> Result<Self, String> {
        let mut chars = s.chars();
        // line-number part
        let mut line_number = String::new();
        let mut spaces_before_colon = 0;
        while let Some(c) = chars.next() {
            match c {
                c if c.is_ascii_digit() => {
                    line_number.push(c);
                }
                ':' => {
                    break;
                }
                ' ' => {
                    spaces_before_colon += 1;
                }
                _ => {
                    Err(format!("Unexpected char '{c}' in line-number"))?;
                }
            };
        }
        if strict {
            if spaces_before_colon != 1 {
                Err(format!(
                    "Got {spaces_before_colon} spaces before colon but expected 1"
                ))?;
            }
            chars
                .next()
                .filter(|c| ' '.eq(c))
                .ok_or(String::from("Expect one space after colon"))?;
        }
        let mut result = Self::new(line_number.parse::<usize>().map_err(|e| e.to_string())?);
        let mut name = String::new();
        while let Some(c) = chars.next() {
            match c {
                c if c.is_alphanumeric() || c == '_' => {
                    name.push(c);
                }
                ',' => {
                    if name.is_empty() {
                        Err(String::from("Got comma but expect a function name"))?;
                    }
                    result.add_name(&name);
                    name.clear();
                    if strict {
                        chars
                            .next()
                            .filter(|c| ' '.eq(c))
                            .ok_or(format!("Expect space after comma"))?;
                    }
                }
                ' ' => {
                    if strict {
                        Err(String::from("Too much spaces in function names"))?;
                    }
                }
                _ => {
                    Err(format!("Unexpected char '{c}' in function names"))?;
                }
            };
        }
        if name.is_empty() {
            Err(String::from(
                "Unexpected End-Of-Line before a function name",
            ))?;
        }
        result.add_name(&name);
        Ok(result)
    }
}

impl FromStr for LineNames {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::parse_from_str(s, false)
    }
}
