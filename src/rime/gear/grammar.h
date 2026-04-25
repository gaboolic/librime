#ifndef RIME_GRAMMAR_H_
#define RIME_GRAMMAR_H_

#include <rime/common.h>
#include <rime/component.h>

namespace rime {

class Config;

class Grammar : public Class<Grammar, Config*> {
 public:
  static constexpr double kDefaultNoGrammarPenalty = -13.815510557964274;

  virtual ~Grammar() {}
  virtual double Query(const string& context,
                       const string& word,
                       bool is_rear) = 0;

  inline static double Evaluate(
      const string& context,
      const string& entry_text,
      double entry_weight,
      bool is_rear,
      Grammar* grammar,
      double no_grammar_penalty = kDefaultNoGrammarPenalty) {
    return entry_weight + (grammar
                               ? grammar->Query(context, entry_text, is_rear)
                               : no_grammar_penalty);
  }
};

}  // namespace rime

#endif  // RIME_GRAMMAR_H_
