use std::ffi::{CStr, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::slice;

use fuzzy_rank::fields::literal::{MessageCandidate, MessageField, MessageQuery, select_top_k};

#[repr(C)]
pub struct FuzzyRankCandidate {
    pub text: *const c_char,
    pub score: f64,
}

fn rank_exact_candidates(
    query: *const c_char,
    candidates: *const FuzzyRankCandidate,
    candidate_count: usize,
    output_indices: *mut usize,
    output_capacity: usize,
) -> usize {
    if query.is_null()
        || candidates.is_null()
        || output_indices.is_null()
        || candidate_count == 0
        || output_capacity == 0
    {
        return 0;
    }

    let query = unsafe { CStr::from_ptr(query) }.to_string_lossy();
    let Some(query) = MessageQuery::new(&query) else {
        return 0;
    };
    let candidates = unsafe { slice::from_raw_parts(candidates, candidate_count) };
    let keys = (0..candidate_count)
        .map(|index| index.to_string())
        .collect::<Vec<_>>();
    let texts = candidates
        .iter()
        .map(|candidate| {
            if candidate.text.is_null() {
                String::new()
            } else {
                unsafe { CStr::from_ptr(candidate.text) }
                    .to_string_lossy()
                    .into_owned()
            }
        })
        .collect::<Vec<_>>();

    let mut matches = Vec::with_capacity(candidate_count);
    for (index, candidate) in candidates.iter().enumerate() {
        if texts[index].is_empty() {
            continue;
        }
        let fields = [MessageField {
            priority: 0,
            value: &texts[index],
        }];
        if let Some(matched) = query.search_rank(MessageCandidate {
            key: &keys[index],
            fields: &fields,
            score: candidate.score,
        }) {
            matches.push(matched);
        }
    }

    select_top_k(&mut matches, output_capacity);
    let output_count = matches.len();
    let output = unsafe { slice::from_raw_parts_mut(output_indices, output_capacity) };
    for (slot, matched) in output.iter_mut().zip(matches.iter()).take(output_count) {
        if let Ok(index) = matched.key.parse::<usize>() {
            *slot = index;
        }
    }
    output_count
}

fn rank_messages(
    query: *const c_char,
    candidates: *const FuzzyRankCandidate,
    candidate_count: usize,
    output_indices: *mut usize,
    output_capacity: usize,
) -> usize {
    rank_exact_candidates(
        query,
        candidates,
        candidate_count,
        output_indices,
        output_capacity,
    )
}

fn rank_transcript_windows(
    query: *const c_char,
    candidates: *const FuzzyRankCandidate,
    candidate_count: usize,
    output_indices: *mut usize,
    output_capacity: usize,
) -> usize {
    // A transcript window is already an ordered context. Passing it through
    // the exact-only message adapter as one field makes phrase and span
    // scoring cross the original Whisper segment boundaries without enabling
    // fuzzy matching.
    rank_exact_candidates(
        query,
        candidates,
        candidate_count,
        output_indices,
        output_capacity,
    )
}

#[unsafe(no_mangle)]
pub extern "C" fn fuzzy_rank_messages(
    query: *const c_char,
    candidates: *const FuzzyRankCandidate,
    candidate_count: usize,
    output_indices: *mut usize,
    output_capacity: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        rank_messages(
            query,
            candidates,
            candidate_count,
            output_indices,
            output_capacity,
        )
    }))
    .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn fuzzy_rank_transcript_windows(
    query: *const c_char,
    candidates: *const FuzzyRankCandidate,
    candidate_count: usize,
    output_indices: *mut usize,
    output_capacity: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        rank_transcript_windows(
            query,
            candidates,
            candidate_count,
            output_indices,
            output_capacity,
        )
    }))
    .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn exact_phrase_is_ranked_before_scattered_terms() {
        let query = CString::new("word1 word2").unwrap();
        let texts = [
            CString::new("word1 separated by several words before word2").unwrap(),
            CString::new("word1 word2 together").unwrap(),
        ];
        let candidates = texts
            .iter()
            .map(|text| FuzzyRankCandidate {
                text: text.as_ptr(),
                score: 0.0,
            })
            .collect::<Vec<_>>();
        let mut output = vec![usize::MAX; candidates.len()];

        let count = fuzzy_rank_messages(
            query.as_ptr(),
            candidates.as_ptr(),
            candidates.len(),
            output.as_mut_ptr(),
            output.len(),
        );

        assert_eq!(count, 2);
        assert_eq!(output, vec![1, 0]);
    }

    #[test]
    fn transcript_window_allows_phrase_across_segment_boundary() {
        let query = CString::new("cannibal mukbang").unwrap();
        let text = CString::new("a cannibal\nmukbang film").unwrap();
        let candidates = [FuzzyRankCandidate {
            text: text.as_ptr(),
            score: 0.0,
        }];
        let mut output = [usize::MAX; 1];

        let count = fuzzy_rank_transcript_windows(
            query.as_ptr(),
            candidates.as_ptr(),
            candidates.len(),
            output.as_mut_ptr(),
            output.len(),
        );

        assert_eq!(count, 1);
        assert_eq!(output, [0]);
    }
}
