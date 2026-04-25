//
// Copyright RIME Developers
// Distributed under the BSD License
//
// simplistic sentence-making
//
// 2011-10-06 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <functional>
#include <list>
#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/grammar.h>
#include <rime/gear/poet.h>

namespace rime {

// internal data structure used during the sentence making process.
// the output line of the algorithm is transformed to an<Sentence>.
struct Line {
  // be sure the pointer to predecessor Line object is stable. it works since
  // pointer to values stored in std::map and std::unordered_map are stable.
  const Line* predecessor;
  // as long as the word graph lives, pointers to entries are valid.
  const DictEntry* entry;
  size_t end_pos;
  double weight;

  static const Line kEmpty;

  bool empty() const { return !predecessor && !entry; }

  string last_word() const { return entry ? entry->text : string(); }

  struct Components {
    vector<const Line*> lines;

    Components(const Line* line) {
      for (const Line* cursor = line; !cursor->empty();
           cursor = cursor->predecessor) {
        lines.push_back(cursor);
      }
    }

    decltype(lines.crbegin()) begin() const { return lines.crbegin(); }
    decltype(lines.crend()) end() const { return lines.crend(); }
  };

  Components components() const { return Components(this); }

  string context() const {
    // look back 2 words
    return empty() ? string()
           : !predecessor || predecessor->empty()
               ? last_word()
               : predecessor->last_word() + last_word();
  }

  vector<string> history_tokens(size_t max_tokens = 2) const {
    vector<string> tokens;
    if (empty()) {
      return tokens;
    }
    for (const Line* cursor = this; cursor && !cursor->empty(); cursor = cursor->predecessor) {
      if (tokens.size() >= max_tokens) {
        break;
      }
      tokens.push_back(cursor->last_word());
    }
    std::reverse(tokens.begin(), tokens.end());
    return tokens;
  }

  GrammarContext grammar_context() const {
    return GrammarContext{context(), history_tokens()};
  }

  vector<size_t> word_lengths() const {
    vector<size_t> lengths;
    size_t last_end_pos = 0;
    for (const auto* c : components()) {
      lengths.push_back(c->end_pos - last_end_pos);
      last_end_pos = c->end_pos;
    }
    return lengths;
  }
};

const Line Line::kEmpty{nullptr, nullptr, 0, 0.0};

inline static Grammar* create_grammar(Config* config) {
  if (!config || !config->IsMap("grammar")) {
    return nullptr;
  }
  string language;
  string model_path;
  if (!config->GetString("grammar/language", &language) &&
      !config->GetString("grammar/model_path", &model_path)) {
    return nullptr;
  }
  if (auto* grammar = Grammar::Require("grammar")) {
    return grammar->Create(config);
  }
  return nullptr;
}

Poet::Poet(const Language* language, Config* config, Compare compare)
    : language_(language),
      grammar_(create_grammar(config)),
      compare_(compare) {
  // Default keeps the legacy grammar-less fallback penalty: log(1e-6).
  // Override grammar/no_grammar_penalty in schema config to tune segmentation.
  if (config) {
    config->GetDouble("grammar/no_grammar_penalty", &no_grammar_penalty_);
  }
}
Poet::~Poet() {}

bool Poet::CompareWeight(const Line& one, const Line& other) {
  return one.weight < other.weight;
}

// returns true if one is less than other.
bool Poet::LeftAssociateCompare(const Line& one, const Line& other) {
  if (one.weight < other.weight)
    return true;
  if (one.weight == other.weight) {
    auto one_word_lens = one.word_lengths();
    auto other_word_lens = other.word_lengths();
    // When scores tie, avoid over-merging adjacent words into longer chunks.
    // Prefer finer-grained segmentations over fewer merged words.
    if (one_word_lens.size() < other_word_lens.size())
      return true;
    if (one_word_lens.size() == other_word_lens.size()) {
      return std::lexicographical_compare(
          other_word_lens.begin(), other_word_lens.end(), one_word_lens.begin(),
          one_word_lens.end());
    }
  }
  return false;
}

using LineList = std::list<Line>;
// keep multiple line candidates per last phrase
using LineCandidates = hash_map<string, LineList>;

static void insert_top_candidate(LineList& candidates,
                                 const Line& new_line,
                                 Poet::Compare compare,
                                 size_t limit) {
  if (limit == 0)
    return;
  auto pos = candidates.begin();
  while (pos != candidates.end() && !compare(*pos, new_line)) {
    ++pos;
  }
  candidates.insert(pos, new_line);
  if (candidates.size() > limit) {
    candidates.pop_back();
  }
}

static vector<const Line*> find_top_candidates(const LineCandidates& candidates,
                                               Poet::Compare compare,
                                               size_t limit) {
  vector<const Line*> top;
  if (limit == 0)
    return top;
  top.reserve(limit + 1);
  for (const auto& group : candidates) {
    for (const auto& candidate : group.second) {
      auto pos = std::upper_bound(
          top.begin(), top.end(), &candidate,
          [&](const Line* a, const Line* b) { return compare(*b, *a); });  // desc
      if (static_cast<size_t>(pos - top.begin()) >= limit)
        continue;
      top.insert(pos, &candidate);
      if (top.size() > limit)
        top.pop_back();
    }
  }
  return top;
}

using UpdateLineCandidate = function<void(const Line& candidate)>;

struct BeamSearch {
  using State = LineCandidates;

  static constexpr int kMaxLineCandidates = 7;

  static void Initiate(State& initial_state) {
    initial_state[""].push_back(Line::kEmpty);
  }

  static void ForEachCandidate(const State& state,
                               Poet::Compare compare,
                               UpdateLineCandidate update) {
    auto top_candidates = find_top_candidates(state, compare, kMaxLineCandidates);
    for (const auto* candidate : top_candidates) {
      update(*candidate);
    }
  }

  static void UpdateState(State& state,
                          const Line& new_line,
                          Poet::Compare compare) {
    insert_top_candidate(state[new_line.last_word()], new_line, compare,
                         kMaxLineCandidates);
  }

  static vector<const Line*> TopLinesInState(const State& final_state,
                                             Poet::Compare compare,
                                             size_t limit) {
    return find_top_candidates(final_state, compare, limit);
  }
};

struct DynamicProgramming {
  using State = LineList;

  static constexpr int kMaxLineCandidates = 32;

  static void Initiate(State& initial_state) { initial_state.push_back(Line::kEmpty); }

  static void ForEachCandidate(const State& state,
                               Poet::Compare compare,
                               UpdateLineCandidate update) {
    for (const auto& candidate : state) {
      update(candidate);
    }
  }

  static void UpdateState(State& state,
                          const Line& new_line,
                          Poet::Compare compare) {
    insert_top_candidate(state, new_line, compare, kMaxLineCandidates);
  }

  static vector<const Line*> TopLinesInState(const State& final_state,
                                             Poet::Compare compare,
                                             size_t limit) {
    vector<const Line*> top_lines;
    if (limit == 0)
      return top_lines;
    top_lines.reserve((std::min)(limit, final_state.size()));
    for (const auto& line : final_state) {
      if (top_lines.size() >= limit)
        break;
      top_lines.push_back(&line);
    }
    return top_lines;
  }
};

an<Sentence> Poet::MakeSentenceFromLine(const Line& line) const {
  if (line.empty())
    return nullptr;
  auto sentence = New<Sentence>(language_);
  for (const auto* c : line.components()) {
    if (!c->entry)
      continue;
    sentence->Extend(*c->entry, c->end_pos, c->weight);
  }
  return sentence;
}

template <class Strategy>
vector<an<Sentence>> Poet::MakeSentencesWithStrategy(
    const WordGraph& graph,
    size_t total_length,
    const string& preceding_text,
    size_t limit) {
  vector<an<Sentence>> sentences;
  if (limit == 0)
    return sentences;
  map<size_t, typename Strategy::State> states;
  Strategy::Initiate(states[0]);
  for (const auto& sv : graph) {
    size_t start_pos = sv.first;
    if (states.find(start_pos) == states.end())
      continue;
    DLOG(INFO) << "start pos: " << start_pos;
    const auto& source_state = states[start_pos];
    const auto update = [this, &states, &sv, start_pos, total_length,
                         &preceding_text](const Line& candidate) {
      for (const auto& ev : sv.second) {
        size_t end_pos = ev.first;
        if (start_pos == 0 && end_pos == total_length)
          continue;  // exclude single word from the result
        DLOG(INFO) << "end pos: " << end_pos;
        bool is_rear = end_pos == total_length;
        auto& target_state = states[end_pos];
        // extend candidates with dict entries on a valid edge.
        const DictEntryList& entries = ev.second;
        for (const auto& entry : entries) {
          const GrammarContext context =
              candidate.empty() ? GrammarContext::FromRawText(preceding_text)
                                : candidate.grammar_context();
          double delta = grammar_
                             ? Grammar::Evaluate(context, entry->text,
                                                 entry->weight, is_rear,
                                                 grammar_.get())
                             : entry->weight + no_grammar_penalty_;
          double weight = candidate.weight + delta;
          Line new_line{&candidate, entry.get(), end_pos, weight};
          DLOG(INFO) << "updated line ending at " << end_pos
                     << " with text: ..." << new_line.last_word()
                     << " weight: " << new_line.weight;
          Strategy::UpdateState(target_state, new_line, compare_);
        }
      }
    };
    Strategy::ForEachCandidate(source_state, compare_, update);
  }
  auto found = states.find(total_length);
  if (found == states.end() || found->second.empty()) {
    return sentences;
  }
  auto top_lines = Strategy::TopLinesInState(found->second, compare_, limit);
  sentences.reserve(top_lines.size());
  for (const auto* line : top_lines) {
    if (auto sentence = MakeSentenceFromLine(*line)) {
      sentences.push_back(sentence);
    }
  }
  return sentences;
}

vector<an<Sentence>> Poet::MakeSentences(const WordGraph& graph,
                                         size_t total_length,
                                         const string& preceding_text,
                                         size_t limit) {
  return grammar_ ? MakeSentencesWithStrategy<BeamSearch>(graph, total_length,
                                                          preceding_text, limit)
                  : MakeSentencesWithStrategy<DynamicProgramming>(
                        graph, total_length, preceding_text, limit);
}

an<Sentence> Poet::MakeSentence(const WordGraph& graph,
                                size_t total_length,
                                const string& preceding_text) {
  auto sentences = MakeSentences(graph, total_length, preceding_text, 1);
  return sentences.empty() ? nullptr : sentences.front();
}

}  // namespace rime
