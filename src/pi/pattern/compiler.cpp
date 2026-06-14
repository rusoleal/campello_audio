#include "compiler.hpp"
#include <campello_audio/audio_source.hpp>
#include <cctype>
#include <cmath>
#include <memory>
#include <random>
#include <sstream>
#include "mini_notation.hpp"

namespace systems::leal::campello_audio::pi {

namespace {

// ---------------------------------------------------------------------------
// Expression Tokenizer
// ---------------------------------------------------------------------------

enum class ExprTokenType {
    End,
    Identifier,
    Number,
    String,
    LParen,
    RParen,
    Dot,
    Comma
};

struct ExprToken {
    ExprTokenType type = ExprTokenType::End;
    std::string text;
    double number = 0.0;
};

class ExprTokenizer {
public:
    explicit ExprTokenizer(const std::string& str) : input(str), pos(0) {}

    ExprToken next() {
        skipWhitespace();
        if (pos >= input.size()) return {ExprTokenType::End};

        char c = input[pos];
        if (c == '(') { ++pos; return {ExprTokenType::LParen}; }
        if (c == ')') { ++pos; return {ExprTokenType::RParen}; }
        if (c == '.') { ++pos; return {ExprTokenType::Dot}; }
        if (c == ',') { ++pos; return {ExprTokenType::Comma}; }
        if (c == '"') return parseString();
        if (std::isdigit(c) || c == '-') return parseNumber();
        if (std::isalpha(c) || c == '_') return parseIdentifier();

        // Unknown character
        ++pos;
        ExprToken t{ExprTokenType::Identifier};
        t.text = std::string(1, c);
        return t;
    }

private:
    void skipWhitespace() {
        while (pos < input.size() && std::isspace(input[pos])) ++pos;
    }

    ExprToken parseString() {
        ++pos; // skip opening "
        std::string value;
        while (pos < input.size() && input[pos] != '"') {
            if (input[pos] == '\\' && pos + 1 < input.size()) {
                ++pos;
                if (input[pos] == '"' || input[pos] == '\\') {
                    value += input[pos];
                } else {
                    value += '\\';
                    value += input[pos];
                }
            } else {
                value += input[pos];
            }
            ++pos;
        }
        if (pos < input.size() && input[pos] == '"') ++pos; // skip closing "
        ExprToken t{ExprTokenType::String};
        t.text = std::move(value);
        return t;
    }

    ExprToken parseNumber() {
        size_t start = pos;
        if (input[pos] == '-') ++pos;
        while (pos < input.size() && std::isdigit(input[pos])) ++pos;
        if (pos < input.size() && input[pos] == '.') {
            ++pos;
            while (pos < input.size() && std::isdigit(input[pos])) ++pos;
        }
        ExprToken t{ExprTokenType::Number};
        t.text = input.substr(start, pos - start);
        t.number = std::stod(t.text);
        return t;
    }

    ExprToken parseIdentifier() {
        size_t start = pos;
        while (pos < input.size() &&
               (std::isalnum(input[pos]) || input[pos] == '_')) {
            ++pos;
        }
        ExprToken t{ExprTokenType::Identifier};
        t.text = input.substr(start, pos - start);
        return t;
    }

    const std::string& input;
    size_t pos;
};

// ---------------------------------------------------------------------------
// Expression AST
// ---------------------------------------------------------------------------

struct ExprNode {
    virtual ~ExprNode() = default;
};

struct StringNode : ExprNode {
    std::string value;
    explicit StringNode(std::string v) : value(std::move(v)) {}
};

struct NumberNode : ExprNode {
    double value = 0.0;
    explicit NumberNode(double v) : value(v) {}
};

struct CallNode : ExprNode {
    std::string name;
    std::vector<std::unique_ptr<ExprNode>> args;
};

struct ChainNode : ExprNode {
    std::unique_ptr<ExprNode> base;
    std::vector<std::unique_ptr<CallNode>> methods;
};

// ---------------------------------------------------------------------------
// Expression Parser
// ---------------------------------------------------------------------------

struct ExprParseCtx {
    ExprTokenizer& tok;
    ExprToken cur;
    bool err = false;
    std::string errMsg;
    explicit ExprParseCtx(ExprTokenizer& t) : tok(t) { cur = tok.next(); }
    void advance() { cur = tok.next(); }
};

std::unique_ptr<ExprNode> parseExpression(ExprParseCtx& ctx);
std::unique_ptr<ExprNode> parseAtom(ExprParseCtx& ctx);
std::unique_ptr<CallNode> parseCall(ExprParseCtx& ctx, std::string name);

std::unique_ptr<ExprNode> parseExpression(ExprParseCtx& ctx) {
    auto base = parseAtom(ctx);
    if (ctx.err || !base) return nullptr;

    if (ctx.cur.type == ExprTokenType::Dot) {
        auto chain = std::make_unique<ChainNode>();
        chain->base = std::move(base);
        while (ctx.cur.type == ExprTokenType::Dot) {
            ctx.advance(); // consume .
            if (ctx.cur.type != ExprTokenType::Identifier) {
                ctx.err = true;
                ctx.errMsg = "Expected method name after '.'";
                return nullptr;
            }
            std::string name = ctx.cur.text;
            ctx.advance();
            auto method = parseCall(ctx, name);
            if (ctx.err || !method) return nullptr;
            chain->methods.push_back(std::move(method));
        }
        return chain;
    }
    return base;
}

std::unique_ptr<ExprNode> parseAtom(ExprParseCtx& ctx) {
    if (ctx.cur.type == ExprTokenType::String) {
        auto node = std::make_unique<StringNode>(ctx.cur.text);
        ctx.advance();
        return node;
    }
    if (ctx.cur.type == ExprTokenType::Identifier) {
        std::string name = ctx.cur.text;
        ctx.advance();
        if (ctx.cur.type == ExprTokenType::LParen) {
            return parseCall(ctx, name);
        }
        // Bare identifier without parens — treat as mini-notation string
        return std::make_unique<StringNode>(name);
    }
    if (ctx.cur.type == ExprTokenType::Number) {
        auto node = std::make_unique<NumberNode>(ctx.cur.number);
        ctx.advance();
        return node;
    }
    ctx.err = true;
    ctx.errMsg = "Unexpected token in expression";
    return nullptr;
}

std::unique_ptr<CallNode> parseCall(ExprParseCtx& ctx, std::string name) {
    auto call = std::make_unique<CallNode>();
    call->name = std::move(name);

    if (ctx.cur.type != ExprTokenType::LParen) {
        ctx.err = true;
        ctx.errMsg = "Expected '(' after function/method name";
        return nullptr;
    }
    ctx.advance(); // consume (

    if (ctx.cur.type != ExprTokenType::RParen) {
        while (true) {
            auto arg = parseExpression(ctx);
            if (ctx.err || !arg) return nullptr;
            call->args.push_back(std::move(arg));
            if (ctx.cur.type == ExprTokenType::RParen) break;
            if (ctx.cur.type != ExprTokenType::Comma) {
                ctx.err = true;
                ctx.errMsg = "Expected ',' or ')' in argument list";
                return nullptr;
            }
            ctx.advance(); // consume ,
        }
    }
    ctx.advance(); // consume )
    return call;
}

// ---------------------------------------------------------------------------
// Compiler helpers
// ---------------------------------------------------------------------------

bool expectArgCount(const CallNode* call, size_t expected, std::string& err) {
    if (call->args.size() != expected) {
        err = call->name + "() expects " + std::to_string(expected) + " argument(s)";
        return false;
    }
    return true;
}

bool expectMinArgs(const CallNode* call, size_t min, std::string& err) {
    if (call->args.size() < min) {
        err = call->name + "() expects at least " + std::to_string(min) + " argument(s)";
        return false;
    }
    return true;
}

const NumberNode* asNumber(const ExprNode* node) {
    return dynamic_cast<const NumberNode*>(node);
}

const StringNode* asString(const ExprNode* node) {
    return dynamic_cast<const StringNode*>(node);
}

const CallNode* asCall(const ExprNode* node) {
    return dynamic_cast<const CallNode*>(node);
}

const ChainNode* asChain(const ExprNode* node) {
    return dynamic_cast<const ChainNode*>(node);
}

// Forward declaration
std::unique_ptr<Pattern> evalExpr(const ExprNode* node, double cycleBeats, std::string& err);

void applyGain(Pattern& pat, float value) {
    for (auto& ev : pat.events) ev.gain = value;
}

void applyPan(Pattern& pat, float value) {
    for (auto& ev : pat.events) ev.pan = value;
}

void applyPitch(Pattern& pat, float value) {
    for (auto& ev : pat.events) ev.pitch = value;
}

void applyLpf(Pattern& pat, float minVal, float maxVal, double periodBeats) {
    ParameterCurve curve;
    curve.targetParam = PatternParam::LpfCutoff;
    curve.type = CurveType::Sine;
    curve.periodBeats = periodBeats;
    curve.minValue = minVal;
    curve.maxValue = maxVal;
    for (auto& ev : pat.events) ev.paramCurves.push_back(curve);
}

void applyHpf(Pattern& pat, float minVal, float maxVal, double periodBeats) {
    ParameterCurve curve;
    curve.targetParam = PatternParam::HpfCutoff;
    curve.type = CurveType::Sine;
    curve.periodBeats = periodBeats;
    curve.minValue = minVal;
    curve.maxValue = maxVal;
    for (auto& ev : pat.events) ev.paramCurves.push_back(curve);
}

void applyCurve(Pattern& pat, PatternParam target, CurveType type,
                float minVal, float maxVal, double periodBeats) {
    ParameterCurve curve;
    curve.targetParam = target;
    curve.type = type;
    curve.periodBeats = periodBeats;
    curve.minValue = minVal;
    curve.maxValue = maxVal;
    for (auto& ev : pat.events) ev.paramCurves.push_back(curve);
}

std::unique_ptr<Pattern> evalCall(const CallNode* call, double cycleBeats, std::string& err) {
    if (call->name == "sound") {
        if (!expectArgCount(call, 1, err)) return nullptr;
        auto strNode = asString(call->args[0].get());
        if (!strNode) { err = "sound() expects a string argument"; return nullptr; }
        auto pat = parseMiniNotation(strNode->value, cycleBeats);
        if (!pat) {
            err = "Failed to parse mini-notation: " + strNode->value;
            return nullptr;
        }
        return pat;
    }

    if (call->name == "stack") {
        if (call->args.empty()) {
            err = "stack() expects at least 1 argument";
            return nullptr;
        }
        // Evaluate all children first to find max length
        std::vector<std::unique_ptr<Pattern>> children;
        double maxLen = cycleBeats;
        for (const auto& arg : call->args) {
            auto child = evalExpr(arg.get(), cycleBeats, err);
            if (!child) return nullptr;
            maxLen = std::max(maxLen, child->lengthInBeats);
            children.push_back(std::move(child));
        }
        auto result = std::make_unique<Pattern>();
        result->lengthInBeats = maxLen;
        for (auto& child : children) {
            double scale = result->lengthInBeats / child->lengthInBeats;
            for (auto& ev : child->events) {
                ev.beat *= scale;
                result->events.push_back(ev);
            }
        }
        std::stable_sort(result->events.begin(), result->events.end(),
                         [](const PatternEvent& a, const PatternEvent& b) {
                             return a.beat < b.beat;
                         });
        return result;
    }

    if (call->name == "cat") {
        if (call->args.empty()) {
            err = "cat() expects at least 1 argument";
            return nullptr;
        }
        std::vector<std::unique_ptr<Pattern>> children;
        double totalLen = 0.0;
        for (const auto& arg : call->args) {
            auto child = evalExpr(arg.get(), cycleBeats, err);
            if (!child) return nullptr;
            totalLen += child->lengthInBeats;
            children.push_back(std::move(child));
        }
        auto result = std::make_unique<Pattern>();
        result->lengthInBeats = totalLen;
        double pos = 0.0;
        for (auto& child : children) {
            for (auto& ev : child->events) {
                ev.beat += pos;
                result->events.push_back(ev);
            }
            pos += child->lengthInBeats;
        }
        std::stable_sort(result->events.begin(), result->events.end(),
                         [](const PatternEvent& a, const PatternEvent& b) {
                             return a.beat < b.beat;
                         });
        return result;
    }

    if (call->name == "rev") {
        if (!expectArgCount(call, 1, err)) return nullptr;
        auto child = evalExpr(call->args[0].get(), cycleBeats, err);
        if (!child) return nullptr;
        double L = child->lengthInBeats;
        for (auto& ev : child->events) {
            if (ev.beat > 0.0) {
                ev.beat = L - ev.beat;
            }
            // beat == 0 stays at boundary
        }
        std::stable_sort(child->events.begin(), child->events.end(),
                         [](const PatternEvent& a, const PatternEvent& b) {
                             return a.beat < b.beat;
                         });
        return child;
    }

    if (call->name == "slow") {
        if (!expectArgCount(call, 2, err)) return nullptr;
        auto factorNode = asNumber(call->args[0].get());
        if (!factorNode) { err = "slow() first argument must be a number"; return nullptr; }
        auto child = evalExpr(call->args[1].get(), cycleBeats, err);
        if (!child) return nullptr;
        double f = factorNode->value;
        if (f <= 0.0) f = 1.0;
        child->lengthInBeats *= f;
        for (auto& ev : child->events) ev.beat *= f;
        return child;
    }

    if (call->name == "fast") {
        if (!expectArgCount(call, 2, err)) return nullptr;
        auto factorNode = asNumber(call->args[0].get());
        if (!factorNode) { err = "fast() first argument must be a number"; return nullptr; }
        auto child = evalExpr(call->args[1].get(), cycleBeats, err);
        if (!child) return nullptr;
        double f = factorNode->value;
        if (f <= 0.0) f = 1.0;
        child->lengthInBeats /= f;
        for (auto& ev : child->events) ev.beat /= f;
        return child;
    }

    if (call->name == "degradeBy") {
        if (call->args.size() != 2 && call->args.size() != 3) {
            err = "degradeBy() expects 2 or 3 arguments";
            return nullptr;
        }
        auto probNode = asNumber(call->args[0].get());
        if (!probNode) { err = "degradeBy() first argument must be a number"; return nullptr; }
        double prob = std::clamp(probNode->value, 0.0, 1.0);

        size_t patternIdx = 1;
        uint32_t seed = 0;
        if (call->args.size() == 3) {
            auto seedNode = asNumber(call->args[1].get());
            if (!seedNode) { err = "degradeBy() seed must be a number"; return nullptr; }
            seed = static_cast<uint32_t>(seedNode->value);
            patternIdx = 2;
        }

        auto child = evalExpr(call->args[patternIdx].get(), cycleBeats, err);
        if (!child) return nullptr;

        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        std::vector<PatternEvent> filtered;
        filtered.reserve(child->events.size());
        for (auto& ev : child->events) {
            if (dist(rng) >= prob) filtered.push_back(ev);
        }
        child->events = std::move(filtered);
        return child;
    }

    err = "Unknown function: " + call->name;
    return nullptr;
}

std::unique_ptr<Pattern> evalExpr(const ExprNode* node, double cycleBeats, std::string& err) {
    if (!node) {
        err = "Internal error: null expression node";
        return nullptr;
    }

    if (auto* str = dynamic_cast<const StringNode*>(node)) {
        auto pat = parseMiniNotation(str->value, cycleBeats);
        if (!pat) {
            err = "Failed to parse mini-notation: " + str->value;
            return nullptr;
        }
        return pat;
    }

    if (auto* call = dynamic_cast<const CallNode*>(node)) {
        return evalCall(call, cycleBeats, err);
    }

    if (auto* chain = dynamic_cast<const ChainNode*>(node)) {
        auto pat = evalExpr(chain->base.get(), cycleBeats, err);
        if (!pat) return nullptr;

        for (const auto& method : chain->methods) {
            if (method->name == "gain") {
                if (!expectArgCount(method.get(), 1, err)) return nullptr;
                auto n = asNumber(method->args[0].get());
                if (!n) { err = "gain() expects a number"; return nullptr; }
                applyGain(*pat, static_cast<float>(n->value));
            } else if (method->name == "pan") {
                if (!expectArgCount(method.get(), 1, err)) return nullptr;
                auto n = asNumber(method->args[0].get());
                if (!n) { err = "pan() expects a number"; return nullptr; }
                applyPan(*pat, static_cast<float>(n->value));
            } else if (method->name == "pitch") {
                if (!expectArgCount(method.get(), 1, err)) return nullptr;
                auto n = asNumber(method->args[0].get());
                if (!n) { err = "pitch() expects a number"; return nullptr; }
                applyPitch(*pat, static_cast<float>(n->value));
            } else if (method->name == "lpf") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "lpf() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyLpf(*pat, static_cast<float>(minN->value),
                         static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "hpf") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "hpf() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyHpf(*pat, static_cast<float>(minN->value),
                         static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "sine") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "sine() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyCurve(*pat, PatternParam::Gain, CurveType::Sine,
                           static_cast<float>(minN->value),
                           static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "saw") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "saw() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyCurve(*pat, PatternParam::Gain, CurveType::Saw,
                           static_cast<float>(minN->value),
                           static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "square") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "square() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyCurve(*pat, PatternParam::Gain, CurveType::Square,
                           static_cast<float>(minN->value),
                           static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "tri") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "tri() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyCurve(*pat, PatternParam::Gain, CurveType::Triangle,
                           static_cast<float>(minN->value),
                           static_cast<float>(maxN->value), periodN->value);
            } else if (method->name == "perlin") {
                if (!expectArgCount(method.get(), 3, err)) return nullptr;
                auto minN = asNumber(method->args[0].get());
                auto maxN = asNumber(method->args[1].get());
                auto periodN = asNumber(method->args[2].get());
                if (!minN || !maxN || !periodN) {
                    err = "perlin() expects three numbers: min, max, period";
                    return nullptr;
                }
                applyCurve(*pat, PatternParam::Gain, CurveType::Perlin,
                           static_cast<float>(minN->value),
                           static_cast<float>(maxN->value), periodN->value);
            } else {
                err = "Unknown method: " + method->name;
                return nullptr;
            }
        }
        return pat;
    }

    err = "Unsupported expression type";
    return nullptr;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PatternCompiler public API
// ---------------------------------------------------------------------------

void PatternCompiler::registerSource(const std::string& label,
                                     std::shared_ptr<AudioSource> source) {
    if (!source || label.empty()) return;
    sources_[label] = std::move(source);
}

std::unique_ptr<Pattern> PatternCompiler::compile(const std::string& expression,
                                                  double cycleBeats) {
    lastError_.clear();
    if (expression.empty()) {
        auto pat = std::make_unique<Pattern>();
        pat->lengthInBeats = cycleBeats;
        return pat;
    }

    ExprTokenizer tokenizer(expression);
    ExprParseCtx ctx(tokenizer);
    auto ast = parseExpression(ctx);

    if (ctx.err) {
        lastError_ = ctx.errMsg;
        return nullptr;
    }
    if (ctx.cur.type != ExprTokenType::End) {
        lastError_ = "Unexpected trailing tokens in expression";
        return nullptr;
    }
    if (!ast) {
        lastError_ = "Empty expression";
        return nullptr;
    }

    std::string err;
    auto pat = evalExpr(ast.get(), cycleBeats, err);
    if (!pat) {
        lastError_ = err;
        return nullptr;
    }
    return pat;
}

void PatternCompiler::reset() {
    lastError_.clear();
    sources_.clear();
}

} // namespace systems::leal::campello_audio::pi
