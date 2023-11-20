use itertools::{
    EitherOrBoth::{Both, Left, Right},
    Itertools,
};

use crate::{error_code::ErrorCode, line_names::LineNames};

pub fn check(yours: &Vec<LineNames>, answer: &Vec<LineNames>) -> Result<(), ErrorCode> {
    for (number, entry) in yours
        .into_iter()
        .zip_longest(answer.into_iter())
        .enumerate()
    {
        let number = number + 1;
        match entry {
            Both(your, answer) => {
                if your != answer {
                    println!("At line {number} get '{your}' but expect '{answer}'\n");
                    Err(ErrorCode::WrongLine)?;
                }
            }
            Left(your) => {
                println!("At line {number} output is more than answer\nGet: '{your}'\n");
                Err(ErrorCode::MoreThanAnswer)?;
            }
            Right(answer) => {
                println!("At line {number} output is less than answer\nExpect: '{answer}'\n");
                Err(ErrorCode::LessThanAnswer)?;
            }
        }
    }
    Ok(())
}
