#ifndef RIME_GRAMMAR_H_
#define RIME_GRAMMAR_H_

#include <rime/common.h>
#include <rime/component.h>

namespace rime {

class Config;

class Grammar : public Class<Grammar, Config*> {
 public:
  virtual ~Grammar() {}
  virtual double Query(const string& context,
                       const string& word,
                       bool is_rear) = 0;

  static double DefaultPenalty() {
    // log(1e-6) ~= -13.81
    return -13.815510557964274;
  }

  inline static double Evaluate(const string& context,
                                const string& entry_text,
                                double entry_weight,
                                bool is_rear,
                                Grammar* grammar) {
    return Evaluate(context, entry_text, entry_weight, is_rear, grammar,
                    DefaultPenalty());
  }

  inline static double Evaluate(const string& context,
                                const string& entry_text,
                                double entry_weight,
                                bool is_rear,
                                Grammar* grammar,
                                double penalty) {
    return entry_weight +
           (grammar ? grammar->Query(context, entry_text, is_rear) : penalty);
  }
};

}  // namespace rime

#endif  // RIME_GRAMMAR_H_
