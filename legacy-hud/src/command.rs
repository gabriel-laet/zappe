//! Constrained living-room command language.
//!
//! Same parser for the on-screen command bar, stdin lines, `--say`, and the
//! whisper.cpp hook. Not a chat model. Unknown utterances are rejected.

use crate::skills::Service;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Command {
    Play {
        query: String,
        service: Option<Service>,
    },
    Pause,
    PlayPause,
    Fullscreen,
    Back,
    Home,
    Search {
        query: String,
        service: Option<Service>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParseError {
    pub input: String,
    pub message: String,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} ({})", self.message, self.input)
    }
}

impl std::error::Error for ParseError {}

/// Tiny grammar:
/// ```text
/// play <query> [on youtube|netflix|prime|disney]
/// search <query> [on youtube|netflix|prime|disney]
/// pause | fullscreen | back | home
/// ```
pub fn parse(input: &str) -> Result<Command, ParseError> {
    let raw = input.trim();
    let folded = normalize(raw);
    if folded.is_empty() {
        return Err(ParseError {
            input: raw.to_string(),
            message: "empty command".into(),
        });
    }

    match folded.as_str() {
        "pause" | "stop" => return Ok(Command::Pause),
        "play" | "play pause" | "playpause" | "resume" => return Ok(Command::PlayPause),
        "fullscreen" | "full screen" | "full" => return Ok(Command::Fullscreen),
        "back" | "go back" | "return" => return Ok(Command::Back),
        "home" | "guide" | "root" => return Ok(Command::Home),
        _ => {}
    }

    let words: Vec<&str> = folded.split_whitespace().collect();
    let verb = words[0];
    let rest = &words[1..];
    match verb {
        "play" => {
            let (query, service) = split_query_service(rest);
            if query.is_empty() {
                return Ok(Command::PlayPause);
            }
            Ok(Command::Play { query, service })
        }
        "search" | "find" => {
            let (query, service) = split_query_service(rest);
            if query.is_empty() {
                return Err(err(raw, "search needs a query"));
            }
            Ok(Command::Search { query, service })
        }
        _ => Err(err(
            raw,
            "unknown command — try play / pause / search / fullscreen / back / home",
        )),
    }
}

fn err(input: &str, message: &str) -> ParseError {
    ParseError {
        input: input.to_string(),
        message: message.into(),
    }
}

fn normalize(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    for ch in input.chars() {
        if ch.is_ascii_alphanumeric() || ch.is_whitespace() || ch == '+' || ch == '-' {
            out.push(ch.to_ascii_lowercase());
        } else if ch == '.' || ch == ',' || ch == '?' || ch == '!' {
            out.push(' ');
        }
    }
    out.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn split_query_service(words: &[&str]) -> (String, Option<Service>) {
    if let Some(i) = words.iter().position(|w| *w == "on") {
        let service_words = &words[i + 1..];
        if let Some(service) = parse_service_phrase(service_words) {
            return (words[..i].join(" "), Some(service));
        }
    }
    if let Some((last, head)) = words.split_last() {
        if !head.is_empty() {
            if let Some(service) = parse_service_phrase(&[*last]) {
                return (head.join(" "), Some(service));
            }
        }
    }
    (words.join(" "), None)
}

fn parse_service_phrase(words: &[&str]) -> Option<Service> {
    let phrase = words.join(" ");
    match phrase.as_str() {
        "youtube" | "yt" | "you tube" => Some(Service::Youtube),
        "netflix" => Some(Service::Netflix),
        "prime" | "prime video" | "amazon" | "amazon prime" => Some(Service::Prime),
        "disney" | "disney+" | "disney plus" | "disneyplus" => Some(Service::Disney),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn singles() {
        assert_eq!(parse("pause").unwrap(), Command::Pause);
        assert_eq!(parse("FULLSCREEN").unwrap(), Command::Fullscreen);
        assert_eq!(parse("back.").unwrap(), Command::Back);
        assert_eq!(parse("home").unwrap(), Command::Home);
        assert_eq!(parse("play").unwrap(), Command::PlayPause);
    }

    #[test]
    fn play_on_service() {
        assert_eq!(
            parse("play lofi hip hop on youtube").unwrap(),
            Command::Play {
                query: "lofi hip hop".into(),
                service: Some(Service::Youtube),
            }
        );
        assert_eq!(
            parse("play Andor on Disney+").unwrap(),
            Command::Play {
                query: "andor".into(),
                service: Some(Service::Disney),
            }
        );
        assert_eq!(
            parse("play reacher prime").unwrap(),
            Command::Play {
                query: "reacher".into(),
                service: Some(Service::Prime),
            }
        );
    }

    #[test]
    fn search() {
        assert_eq!(
            parse("search the diplomat on netflix").unwrap(),
            Command::Search {
                query: "the diplomat".into(),
                service: Some(Service::Netflix),
            }
        );
    }

    #[test]
    fn rejects_chat() {
        assert!(parse("what's the weather").is_err());
        assert!(parse("please book a flight to oslo").is_err());
        assert!(parse("").is_err());
    }
}
