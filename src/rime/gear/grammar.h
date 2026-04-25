#ifndef RIME_GRAMMAR_H_
#define RIME_GRAMMAR_H_

#include <sstream>
#include <rime/common.h>
#include <rime/component.h>

namespace rime {

class Config;

struct GrammarContext {
  string raw_text;
  vector<string> history_tokens;

  GrammarContext() = default;
  GrammarContext(string raw, vector<string> tokens = {})
      : raw_text(std::move(raw)),
        history_tokens(std::move(tokens)) {}

  bool empty() const { return raw_text.empty() && history_tokens.empty(); }

  string legacy_context_text() const {
    if (!raw_text.empty()) {
      return raw_text;
    }
    string result;
    for (const auto& token : history_tokens) {
      result += token;
    }
    return result;
  }

  static vector<string> TokenizeRawText(const string& text) {
    vector<string> tokens;
    std::istringstream stream(text);
    string token;
    while (stream >> token) {
      tokens.push_back(token);
    }
    if (tokens.empty() && !text.empty()) {
      tokens.push_back(text);
    }
    return tokens;
  }

  static GrammarContext FromRawText(const string& text) {
    return GrammarContext{text, TokenizeRawText(text)};
  }
};

class Grammar : public Class<Grammar, Config*> {
 public:
  virtual ~Grammar() {}
  virtual double Query(const GrammarContext& context,
                       const string& word,
                       bool is_rear) {
    return Query(context.legacy_context_text(), word, is_rear);
  }
  virtual double Query(const string& context,
                       const string& word,
                       bool is_rear) = 0;

  inline static double Evaluate(const GrammarContext& context,
                                const string& entry_text,
                                double entry_weight,
                                bool is_rear,
                                Grammar* grammar) {
    // log(1e-6) ≈ -13.81
    const double kPenalty = -13.815510557964274;
    return entry_weight +
           (grammar ? grammar->Query(context, entry_text, is_rear) : kPenalty);
  }

  inline static double Evaluate(const string& context,
                                const string& entry_text,
                                double entry_weight,
                                bool is_rear,
                                Grammar* grammar) {
    return Evaluate(GrammarContext::FromRawText(context),
                    entry_text,
                    entry_weight,
                    is_rear,
                    grammar);
  }
};

}  // namespace rime

#endif  // RIME_GRAMMAR_H_
