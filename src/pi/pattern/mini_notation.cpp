#include "mini_notation.hpp"
#include <cctype>
#include <cmath>
#include <memory>
#include <vector>

namespace systems::leal::campello_audio::pi {

namespace {

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokenType {
    End, Word, Number, Star, LParen, RParen, LBracket, RBracket,
    Comma, Rest, LessThan, GreaterThan, Slash, Question, Invalid
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    double number = 0.0;
};

class Tokenizer {
public:
    explicit Tokenizer(const std::string& str) : input(str), pos(0) {}

    Token next() {
        skipWhitespace();
        if (pos >= input.size()) return {TokenType::End};
        char c = input[pos];
        if (c == '*') { ++pos; return {TokenType::Star}; }
        if (c == '(') { ++pos; return {TokenType::LParen}; }
        if (c == ')') { ++pos; return {TokenType::RParen}; }
        if (c == '[') { ++pos; return {TokenType::LBracket}; }
        if (c == ']') { ++pos; return {TokenType::RBracket}; }
        if (c == ',') { ++pos; return {TokenType::Comma}; }
        if (c == '<') { ++pos; return {TokenType::LessThan}; }
        if (c == '>') { ++pos; return {TokenType::GreaterThan}; }
        if (c == '/') { ++pos; return {TokenType::Slash}; }
        if (c == '?') { ++pos; return {TokenType::Question}; }
        if (c == '~' || c == '.') { ++pos; return {TokenType::Rest}; }
        if (std::isdigit(c) || c == '-') {
            size_t start = pos;
            if (c == '-') ++pos;
            while (pos < input.size() && std::isdigit(input[pos])) ++pos;
            if (pos < input.size() && input[pos] == '.') {
                ++pos;
                while (pos < input.size() && std::isdigit(input[pos])) ++pos;
            }
            Token t{TokenType::Number};
            t.text = input.substr(start, pos - start);
            t.number = std::stod(t.text);
            return t;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = pos;
            while (pos < input.size() &&
                   (std::isalnum(input[pos]) || input[pos] == '_' ||
                    input[pos] == ':' || input[pos] == '-')) ++pos;
            Token t{TokenType::Word};
            t.text = input.substr(start, pos - start);
            return t;
        }
        ++pos;
        return {TokenType::Invalid};
    }
private:
    void skipWhitespace() {
        while (pos < input.size() && std::isspace(input[pos])) ++pos;
    }
    const std::string& input;
    size_t pos;
};

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------

struct AstNode {
    virtual ~AstNode() = default;
};

struct WordNode : AstNode {
    std::string word;
    explicit WordNode(std::string w) : word(std::move(w)) {}
};

struct RestNode : AstNode {};

struct EuclidNode : AstNode {
    std::string word;
    int hits = 0;
    int steps = 0;
    EuclidNode(std::string w, int h, int s) : word(std::move(w)), hits(h), steps(s) {}
};

struct RepNode : AstNode {
    std::unique_ptr<AstNode> child;
    int count = 1;
    RepNode(std::unique_ptr<AstNode> c, int n) : child(std::move(c)), count(n) {}
};

struct SeqNode : AstNode {
    std::vector<std::unique_ptr<AstNode>> children;
};

struct SlowNode : AstNode {
    std::unique_ptr<AstNode> child;
    int factor = 1;
    SlowNode(std::unique_ptr<AstNode> c, int f)
        : child(std::move(c)), factor(std::max(f, 1)) {}
};

struct ProbNode : AstNode {
    std::unique_ptr<AstNode> child;
    double probability = 0.5;
    ProbNode(std::unique_ptr<AstNode> c, double p)
        : child(std::move(c)), probability(std::clamp(p, 0.0, 1.0)) {}
};

struct SlowCatNode : AstNode {
    std::vector<std::unique_ptr<AstNode>> children;
};

struct ParallelNode : AstNode {
    std::vector<std::unique_ptr<AstNode>> children;
};

// ---------------------------------------------------------------------------
// Parser -> AST
// ---------------------------------------------------------------------------

struct ParseCtx {
    Tokenizer& tok;
    Token cur;
    bool err = false;
    std::string errMsg;
    explicit ParseCtx(Tokenizer& t) : tok(t) { cur = tok.next(); }
    void next() { cur = tok.next(); }
};

std::unique_ptr<AstNode> parseFactor(ParseCtx& ctx);
std::unique_ptr<AstNode> parseElem(ParseCtx& ctx);
std::unique_ptr<AstNode> parseSeq(ParseCtx& ctx);
std::unique_ptr<AstNode> parsePattern(ParseCtx& ctx);
double getWeight(const AstNode* node);

std::unique_ptr<AstNode> parseFactor(ParseCtx& ctx) {
    switch (ctx.cur.type) {
        case TokenType::Word: {
            std::string w = ctx.cur.text;
            ctx.next();
            if (ctx.cur.type == TokenType::LParen) {
                ctx.next(); // (
                if (ctx.cur.type != TokenType::Number) {
                    ctx.err = true; ctx.errMsg = "Expected hits"; return nullptr;
                }
                int hits = static_cast<int>(std::round(ctx.cur.number));
                ctx.next();
                if (ctx.cur.type != TokenType::Comma) {
                    ctx.err = true; ctx.errMsg = "Expected comma"; return nullptr;
                }
                ctx.next(); // ,
                if (ctx.cur.type != TokenType::Number) {
                    ctx.err = true; ctx.errMsg = "Expected steps"; return nullptr;
                }
                int steps = static_cast<int>(std::round(ctx.cur.number));
                ctx.next();
                if (ctx.cur.type != TokenType::RParen) {
                    ctx.err = true; ctx.errMsg = "Expected )"; return nullptr;
                }
                ctx.next(); // )
                return std::make_unique<EuclidNode>(w, hits, steps);
            }
            return std::make_unique<WordNode>(w);
        }
        case TokenType::Rest:
            ctx.next();
            return std::make_unique<RestNode>();
        case TokenType::LBracket: {
            ctx.next(); // [
            auto seq = parseSeq(ctx);
            if (ctx.err) return nullptr;
            if (ctx.cur.type != TokenType::RBracket) {
                ctx.err = true; ctx.errMsg = "Expected ]"; return nullptr;
            }
            ctx.next(); // ]
            return seq;
        }
        case TokenType::LessThan: {
            ctx.next(); // <
            auto sc = std::make_unique<SlowCatNode>();
            while (ctx.cur.type != TokenType::GreaterThan &&
                   ctx.cur.type != TokenType::End &&
                   ctx.cur.type != TokenType::RBracket &&
                   ctx.cur.type != TokenType::Comma) {
                auto elem = parseElem(ctx);
                if (ctx.err || !elem) return nullptr;
                sc->children.push_back(std::move(elem));
            }
            if (ctx.cur.type != TokenType::GreaterThan) {
                ctx.err = true; ctx.errMsg = "Expected >"; return nullptr;
            }
            ctx.next(); // >
            return sc;
        }
        case TokenType::Invalid:
            ctx.err = true;
            ctx.errMsg = "Invalid character in input";
            return nullptr;
        default:
            ctx.err = true;
            ctx.errMsg = "Unexpected token";
            return nullptr;
    }
}

std::unique_ptr<AstNode> parseElem(ParseCtx& ctx) {
    auto base = parseFactor(ctx);
    if (ctx.err || !base) return nullptr;

    // Repetition: *n
    if (ctx.cur.type == TokenType::Star) {
        ctx.next(); // *
        if (ctx.cur.type != TokenType::Number) {
            ctx.err = true; ctx.errMsg = "Expected number after *"; return nullptr;
        }
        int n = static_cast<int>(std::round(ctx.cur.number));
        ctx.next();
        if (n < 1) n = 1;
        base = std::make_unique<RepNode>(std::move(base), n);
    }

    // Slow: /n
    if (ctx.cur.type == TokenType::Slash) {
        ctx.next(); // /
        if (ctx.cur.type != TokenType::Number) {
            ctx.err = true; ctx.errMsg = "Expected number after /"; return nullptr;
        }
        int n = static_cast<int>(std::round(ctx.cur.number));
        ctx.next();
        if (n < 1) n = 1;
        base = std::make_unique<SlowNode>(std::move(base), n);
    }

    // Probability: ? or ?n
    if (ctx.cur.type == TokenType::Question) {
        ctx.next(); // ?
        double prob = 0.5;
        if (ctx.cur.type == TokenType::Number) {
            prob = std::clamp(ctx.cur.number, 0.0, 1.0);
            ctx.next();
        }
        base = std::make_unique<ProbNode>(std::move(base), prob);
    }

    return base;
}

std::unique_ptr<AstNode> parseSeq(ParseCtx& ctx) {
    auto seq = std::make_unique<SeqNode>();
    while (ctx.cur.type != TokenType::End &&
           ctx.cur.type != TokenType::RBracket &&
           ctx.cur.type != TokenType::GreaterThan &&
           ctx.cur.type != TokenType::Comma) {
        auto elem = parseElem(ctx);
        if (ctx.err || !elem) return nullptr;
        seq->children.push_back(std::move(elem));
    }
    return seq;
}

std::unique_ptr<AstNode> parsePattern(ParseCtx& ctx) {
    auto first = parseSeq(ctx);
    if (ctx.err || !first) return nullptr;

    if (ctx.cur.type == TokenType::Comma) {
        auto par = std::make_unique<ParallelNode>();
        par->children.push_back(std::move(first));
        while (ctx.cur.type == TokenType::Comma) {
            ctx.next(); // consume ,
            auto seq = parseSeq(ctx);
            if (ctx.err || !seq) return nullptr;
            par->children.push_back(std::move(seq));
        }
        return par;
    }
    return first;
}

// ---------------------------------------------------------------------------
// Euclidean rhythm (Bjorklund)
// ---------------------------------------------------------------------------

std::vector<bool> euclideanRhythm(int hits, int steps) {
    std::vector<bool> result(steps, false);
    if (steps <= 0 || hits <= 0) return result;
    if (hits >= steps) {
        std::fill(result.begin(), result.end(), true);
        return result;
    }
    // Bresenham-style distribution — matches TidalCycles' euclid output.
    for (int i = 0; i < steps; ++i) {
        if ((i * hits) % steps < hits) {
            result[i] = true;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// AST -> Pattern events
// ---------------------------------------------------------------------------

void compileNode(const AstNode* node, std::vector<PatternEvent>& out,
                 double start, double length);
size_t computeExpansionFactor(const AstNode* node);

double getWeight(const AstNode* node) {
    if (auto* s = dynamic_cast<const SlowNode*>(node)) {
        return static_cast<double>(s->factor) * getWeight(s->child.get());
    }
    if (auto* p = dynamic_cast<const ProbNode*>(node)) {
        return getWeight(p->child.get());
    }
    if (auto* r = dynamic_cast<const RepNode*>(node)) {
        return getWeight(r->child.get());
    }
    return 1.0;
}

void compileSeq(const SeqNode* seq, std::vector<PatternEvent>& out,
                double start, double length) {
    if (seq->children.empty()) return;
    double totalWeight = 0.0;
    for (const auto& child : seq->children) {
        totalWeight += getWeight(child.get());
    }
    if (totalWeight <= 0.0) return;
    double pos = start;
    for (const auto& child : seq->children) {
        double w = getWeight(child.get());
        double childLen = length * (w / totalWeight);
        compileNode(child.get(), out, pos, childLen);
        pos += childLen;
    }
}

void compileNode(const AstNode* node, std::vector<PatternEvent>& out,
                 double start, double length) {
    if (!node) return;
    if (auto* w = dynamic_cast<const WordNode*>(node)) {
        PatternEvent ev;
        ev.beat = start;
        ev.sourceLabel = w->word;
        out.push_back(ev);
    } else if (dynamic_cast<const RestNode*>(node)) {
        // no event
    } else if (auto* e = dynamic_cast<const EuclidNode*>(node)) {
        auto rhythm = euclideanRhythm(e->hits, e->steps);
        double stepLen = length / static_cast<double>(e->steps);
        for (int i = 0; i < e->steps; ++i) {
            if (rhythm[i]) {
                PatternEvent ev;
                ev.beat = start + static_cast<double>(i) * stepLen;
                ev.sourceLabel = e->word;
                out.push_back(ev);
            }
        }
    } else if (auto* r = dynamic_cast<const RepNode*>(node)) {
        if (r->count <= 0) return;
        double repLen = length / static_cast<double>(r->count);
        for (int i = 0; i < r->count; ++i) {
            double rs = start + static_cast<double>(i) * repLen;
            compileNode(r->child.get(), out, rs, repLen);
        }
    } else if (auto* s = dynamic_cast<const SlowNode*>(node)) {
        compileNode(s->child.get(), out, start, length);
    } else if (auto* p = dynamic_cast<const ProbNode*>(node)) {
        std::vector<PatternEvent> temp;
        compileNode(p->child.get(), temp, start, length);
        for (auto& ev : temp) {
            ev.probability = static_cast<float>(p->probability);
            out.push_back(ev);
        }
    } else if (auto* s = dynamic_cast<const SeqNode*>(node)) {
        compileSeq(s, out, start, length);
    } else if (auto* sc = dynamic_cast<const SlowCatNode*>(node)) {
        // SlowCat distributes children evenly across allocated time.
        // When at the root, parseMiniNotation expands total length.
        if (sc->children.empty()) return;
        double childLen = length / static_cast<double>(sc->children.size());
        for (size_t i = 0; i < sc->children.size(); ++i) {
            double cs = start + static_cast<double>(i) * childLen;
            compileNode(sc->children[i].get(), out, cs, childLen);
        }
    } else if (auto* p = dynamic_cast<const ParallelNode*>(node)) {
        // Find max expansion factor among children.
        // The 'length' param is already expanded by the top-level.
        size_t maxFactor = 1;
        for (const auto& child : p->children) {
            maxFactor = std::max(maxFactor, computeExpansionFactor(child.get()));
        }
        double baseCycle = length / static_cast<double>(maxFactor);
        for (const auto& child : p->children) {
            size_t childFactor = computeExpansionFactor(child.get());
            double childLen = baseCycle * static_cast<double>(childFactor);
            if (childFactor < maxFactor) {
                size_t repeats = maxFactor / childFactor;
                for (size_t r = 0; r < repeats; ++r) {
                    double offset = start + static_cast<double>(r) * childLen;
                    compileNode(child.get(), out, offset, childLen);
                }
            } else {
                compileNode(child.get(), out, start, length);
            }
        }
    }
}

// Compute how many cycles a SlowCat at the root expands to.
size_t computeExpansionFactor(const AstNode* node) {
    if (auto* sc = dynamic_cast<const SlowCatNode*>(node)) {
        return std::max(size_t(1), sc->children.size());
    }
    if (auto* par = dynamic_cast<const ParallelNode*>(node)) {
        size_t maxFactor = 1;
        for (const auto& child : par->children) {
            maxFactor = std::max(maxFactor, computeExpansionFactor(child.get()));
        }
        return maxFactor;
    }
    if (auto* seq = dynamic_cast<const SeqNode*>(node)) {
        if (seq->children.size() == 1) {
            return computeExpansionFactor(seq->children[0].get());
        }
    }
    if (auto* s = dynamic_cast<const SlowNode*>(node)) {
        return computeExpansionFactor(s->child.get());
    }
    if (auto* p = dynamic_cast<const ProbNode*>(node)) {
        return computeExpansionFactor(p->child.get());
    }
    return 1;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::unique_ptr<Pattern> parseMiniNotation(const std::string& input,
                                           double cycleBeats) {
    if (input.empty()) {
        auto pat = std::make_unique<Pattern>();
        pat->lengthInBeats = cycleBeats;
        return pat;
    }

    Tokenizer tokenizer(input);
    ParseCtx ctx(tokenizer);
    auto ast = parsePattern(ctx);

    if (ctx.err) return nullptr;
    if (ctx.cur.type != TokenType::End) return nullptr;
    if (!ast) {
        auto pat = std::make_unique<Pattern>();
        pat->lengthInBeats = cycleBeats;
        return pat;
    }

    size_t expansion = computeExpansionFactor(ast.get());
    double expandedLength = cycleBeats * static_cast<double>(expansion);

    std::vector<PatternEvent> events;
    compileNode(ast.get(), events, 0.0, expandedLength);

    std::sort(events.begin(), events.end(),
              [](const PatternEvent& a, const PatternEvent& b) {
                  return a.beat < b.beat;
              });

    auto pat = std::make_unique<Pattern>();
    pat->lengthInBeats = expandedLength;
    pat->events = std::move(events);
    return pat;
}

} // namespace systems::leal::campello_audio::pi
