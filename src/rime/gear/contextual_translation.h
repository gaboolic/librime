//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/common.h>
#include <rime/gear/grammar.h>
#include <rime/translation.h>

namespace rime {

class Candidate;
class Grammar;
class Phrase;

class ContextualTranslation : public PrefetchTranslation {
 public:
  ContextualTranslation(an<Translation> translation,
                        string input,
                        GrammarContext preceding_context,
                        Grammar* grammar)
      : PrefetchTranslation(translation),
        input_(input),
        preceding_context_(std::move(preceding_context)),
        grammar_(grammar) {}

 protected:
  bool Replenish() override;

 private:
  an<Phrase> Evaluate(an<Phrase> phrase);
  void AppendToCache(vector<of<Phrase>>& queue);

  string input_;
  GrammarContext preceding_context_;
  Grammar* grammar_;
};

}  // namespace rime
