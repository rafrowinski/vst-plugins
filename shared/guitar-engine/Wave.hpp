// Swarm Synth - the user-defined oscillator wave.
//
// The wave is stored as one short text and compiled ONCE into a lookup table of kWaveSize values;
// the audio code only ever reads the table. Three text forms:
//   "n:x,y;x,y;..."   points joined by a smooth curve   (x = position in the cycle 0..1, y = 0..1)
//   "l:x,y;x,y;..."   points joined by straight lines
//   "f:expression"    a formula in x (0..1 over one cycle), automatically scaled to fill 0..1
//   "w:base64"        one cycle loaded from a WAV file: 256 samples, 16-bit little endian, base64
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace swsyn {

enum { kWaveSize = 1024, kMaxNodes = 32, kWavSamples = 256 };

// y is 0..1 in the text; the oscillator maps it to -1..+1
static const char* const kDefaultWave = "n:0,0.5;0.25,1;0.75,0";

struct WaveNode { float x, y; };

// --------------------------------------------------------------------------------------------------------------------
// formula evaluator (recursive descent, locale independent)

class WaveExpr
{
    const char* fS;
    size_t fI = 0;
    double fX = 0.0;
    int fDepth = 0;
    std::string fErr;

    void skip() { while (fS[fI] == ' ' || fS[fI] == '\t') ++fI; }
    bool eat(char c) { skip(); if (fS[fI] == c) { ++fI; return true; } return false; }
    void fail(const std::string& m) { if (fErr.empty()) fErr = m; }

    static double frac(double v) { return v - std::floor(v); }

    double number()
    {
        double v = 0.0;
        bool any = false;
        while (fS[fI] >= '0' && fS[fI] <= '9') { v = v * 10.0 + (fS[fI++] - '0'); any = true; }
        if (fS[fI] == '.')
        {
            ++fI;
            double scale = 0.1;
            while (fS[fI] >= '0' && fS[fI] <= '9') { v += (fS[fI++] - '0') * scale; scale *= 0.1; any = true; }
        }
        if (!any) { fail("number expected"); return 0.0; }
        if (fS[fI] == 'e' || fS[fI] == 'E')
        {
            const size_t save = fI;
            ++fI;
            int sign = 1, ex = 0;
            if (fS[fI] == '+') ++fI; else if (fS[fI] == '-') { sign = -1; ++fI; }
            if (fS[fI] >= '0' && fS[fI] <= '9')
            {
                while (fS[fI] >= '0' && fS[fI] <= '9') ex = std::min(400, ex * 10 + (fS[fI++] - '0'));
                v *= std::pow(10.0, sign * ex);
            }
            else fI = save; // it was the constant e or an identifier, not an exponent
        }
        return v;
    }

    double call(const std::string& name)
    {
        double a[3] = { 0, 0, 0 };
        int n = 0;
        if (!eat(')'))
        {
            for (;;)
            {
                const double v = expr();
                if (n < 3) a[n] = v;
                ++n;
                if (eat(')')) break;
                if (!eat(',') && !eat(';')) { fail("missing ) after " + name); return 0.0; }
            }
        }
        const auto need = [&](int k) { if (n != k) fail(name + "() takes " + std::to_string(k) + " argument" + (k == 1 ? "" : "s")); return n == k; };

        if (name == "sin")   return need(1) ? std::sin(a[0]) : 0.0;
        if (name == "cos")   return need(1) ? std::cos(a[0]) : 0.0;
        if (name == "tan")   return need(1) ? std::tan(a[0]) : 0.0;
        if (name == "asin")  return need(1) ? std::asin(a[0]) : 0.0;
        if (name == "acos")  return need(1) ? std::acos(a[0]) : 0.0;
        if (name == "atan")  return need(1) ? std::atan(a[0]) : 0.0;
        if (name == "sinh")  return need(1) ? std::sinh(a[0]) : 0.0;
        if (name == "cosh")  return need(1) ? std::cosh(a[0]) : 0.0;
        if (name == "tanh")  return need(1) ? std::tanh(a[0]) : 0.0;
        if (name == "abs")   return need(1) ? std::fabs(a[0]) : 0.0;
        if (name == "sqrt")  return need(1) ? std::sqrt(a[0]) : 0.0;
        if (name == "exp")   return need(1) ? std::exp(a[0]) : 0.0;
        if (name == "ln" || name == "log") return need(1) ? std::log(a[0]) : 0.0;
        if (name == "log10") return need(1) ? std::log10(a[0]) : 0.0;
        if (name == "floor") return need(1) ? std::floor(a[0]) : 0.0;
        if (name == "ceil")  return need(1) ? std::ceil(a[0]) : 0.0;
        if (name == "round") return need(1) ? std::round(a[0]) : 0.0;
        if (name == "frac")  return need(1) ? frac(a[0]) : 0.0;
        if (name == "sign")  return need(1) ? double((a[0] > 0) - (a[0] < 0)) : 0.0;
        if (name == "tri")   return need(1) ? 1.0 - std::fabs(2.0 * frac(a[0]) - 1.0) : 0.0;  // 0 -> 1 -> 0 per unit
        if (name == "saw")   return need(1) ? frac(a[0]) : 0.0;                               // ramp 0 -> 1 per unit
        if (name == "sqr")   return need(1) ? (frac(a[0]) < 0.5 ? 1.0 : 0.0) : 0.0;           // square per unit
        if (name == "min")   return need(2) ? std::min(a[0], a[1]) : 0.0;
        if (name == "max")   return need(2) ? std::max(a[0], a[1]) : 0.0;
        if (name == "pow")   return need(2) ? std::pow(a[0], a[1]) : 0.0;
        if (name == "mod")   return need(2) ? (a[1] != 0.0 ? a[0] - a[1] * std::floor(a[0] / a[1]) : 0.0) : 0.0;
        if (name == "step")  return need(2) ? (a[1] >= a[0] ? 1.0 : 0.0) : 0.0;               // step(edge, v)
        if (name == "clamp") return need(3) ? std::min(a[2], std::max(a[1], a[0])) : 0.0;     // clamp(v, lo, hi)
        if (name == "mix")   return need(3) ? a[0] + (a[1] - a[0]) * a[2] : 0.0;              // mix(a, b, amount)
        fail("unknown function " + name + "()");
        return 0.0;
    }

    double primary()
    {
        skip();
        const char c = fS[fI];
        if (c == '(')
        {
            ++fI;
            const double v = expr();
            if (!eat(')')) fail("missing )");
            return v;
        }
        if ((c >= '0' && c <= '9') || c == '.') return number();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        {
            std::string name;
            while ((fS[fI] >= 'a' && fS[fI] <= 'z') || (fS[fI] >= 'A' && fS[fI] <= 'Z') || (fS[fI] >= '0' && fS[fI] <= '9'))
            {
                char ch = fS[fI++];
                if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
                name += ch;
            }
            if (eat('(')) return call(name);
            if (name == "x" || name == "t") return fX;
            if (name == "pi") return 3.14159265358979323846;
            if (name == "e") return 2.71828182845904523536;
            fail("unknown name " + name);
            return 0.0;
        }
        fail(c == '\0' ? "the formula ends too early" : std::string("unexpected character ") + c);
        return 0.0;
    }

    double power()
    {
        const double b = primary();
        skip();
        if (fS[fI] == '^') { ++fI; return std::pow(b, unary()); }
        return b;
    }

    double unary()
    {
        skip();
        if (fS[fI] == '-') { ++fI; return -unary(); }
        if (fS[fI] == '+') { ++fI; return unary(); }
        return power();
    }

    double term()
    {
        double v = unary();
        for (;;)
        {
            skip();
            if (fS[fI] == '*') { ++fI; v *= unary(); }
            else if (fS[fI] == '/') { ++fI; const double d = unary(); v = d != 0.0 ? v / d : 0.0; }
            else return v;
        }
    }

    double expr()
    {
        if (++fDepth > 60) { fail("too many nested brackets"); --fDepth; return 0.0; }
        double v = term();
        for (;;)
        {
            skip();
            if (fS[fI] == '+') { ++fI; v += term(); }
            else if (fS[fI] == '-') { ++fI; v -= term(); }
            else break;
        }
        --fDepth;
        return v;
    }

public:
    explicit WaveExpr(const char* s) : fS(s) {}

    // returns false and fills err when the text is not a valid formula
    bool eval(double x, double& out, std::string& err)
    {
        fI = 0; fX = x; fDepth = 0; fErr.clear();
        out = expr();
        skip();
        if (fErr.empty() && fS[fI] != '\0') fErr = std::string("unexpected character ") + fS[fI];
        if (!fErr.empty()) { err = fErr; return false; }
        if (!std::isfinite(out)) out = 0.0;
        return true;
    }
};

// --------------------------------------------------------------------------------------------------------------------
// points

inline std::string waveFloat(float f)
{
    char b[48];
    const std::to_chars_result r = std::to_chars(b, b + sizeof(b), std::round(f * 10000.f) / 10000.f, std::chars_format::fixed);
    return std::string(b, r.ptr);
}

inline std::string nodesToString(const std::vector<WaveNode>& nodes, bool smooth)
{
    std::string s = smooth ? "n:" : "l:";
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (i > 0) s += ';';
        s += waveFloat(nodes[i].x) + "," + waveFloat(nodes[i].y);
    }
    return s;
}

inline bool parseNodes(const char* s, std::vector<WaveNode>& nodes)
{
    nodes.clear();
    const char* const end = s + std::strlen(s);
    while (s < end && int(nodes.size()) < kMaxNodes)
    {
        WaveNode n;
        std::from_chars_result r = std::from_chars(s, end, n.x);
        if (r.ec != std::errc() || r.ptr >= end || *r.ptr != ',') return false;
        r = std::from_chars(r.ptr + 1, end, n.y);
        if (r.ec != std::errc()) return false;
        if (!std::isfinite(n.x) || !std::isfinite(n.y)) return false;
        n.x = std::min(0.9999f, std::max(0.f, n.x));
        n.y = std::min(1.f, std::max(0.f, n.y));
        nodes.push_back(n);
        s = r.ptr;
        if (s < end && *s == ';') ++s;
    }
    std::sort(nodes.begin(), nodes.end(), [](const WaveNode& a, const WaveNode& b) { return a.x < b.x; });
    return !nodes.empty();
}

// value of the periodic curve through the (sorted) points at phase x
inline float evalNodes(const std::vector<WaveNode>& nd, bool smooth, float x)
{
    const int n = int(nd.size());
    if (n == 0) return 0.f;
    if (n == 1) return nd[0].y;

    // segment k -> k+1 containing x (the last point connects to the first one a cycle later)
    int k = n - 1;
    for (int i = 0; i < n; ++i) if (x >= nd[i].x) k = i; else break;
    if (x < nd[0].x) { k = n - 1; x += 1.f; }

    const auto X = [&](int i) { return i < 0 ? nd[i + n].x - 1.f : (i >= n ? nd[i - n].x + 1.f : nd[i].x); };
    const auto Y = [&](int i) { return nd[((i % n) + n) % n].y; };

    const float x1 = X(k), x2 = X(k + 1), y1 = Y(k), y2 = Y(k + 1);
    const float h = x2 - x1;
    if (h < 1e-6f) return y1;
    const float t = std::min(1.f, std::max(0.f, (x - x1) / h));
    if (!smooth) return y1 + (y2 - y1) * t;

    const float d0 = X(k + 1) - X(k - 1), d1 = X(k + 2) - X(k);
    const float m1 = d0 > 1e-6f ? (y2 - Y(k - 1)) / d0 : 0.f;
    const float m2 = d1 > 1e-6f ? (Y(k + 2) - y1) / d1 : 0.f;
    const float t2 = t * t, t3 = t2 * t;
    const float v = (2 * t3 - 3 * t2 + 1) * y1 + (t3 - 2 * t2 + t) * h * m1 + (-2 * t3 + 3 * t2) * y2 + (t3 - t2) * h * m2;
    return std::min(1.f, std::max(0.f, v));
}

// --------------------------------------------------------------------------------------------------------------------
// one cycle of samples (values 0..1) <-> base64 text

inline std::string encodeSamples(const float* cyc)
{
    static const char* const kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char raw[kWavSamples * 2];
    for (int i = 0; i < kWavSamples; ++i)
    {
        const int v = int(std::lround(std::min(1.f, std::max(0.f, cyc[i])) * 65535.f));
        raw[2 * i] = (unsigned char)(v & 255); raw[2 * i + 1] = (unsigned char)(v >> 8);
    }
    std::string s;
    for (int i = 0; i < kWavSamples * 2; i += 3)
    {
        const int n = std::min(3, kWavSamples * 2 - i);
        const unsigned v = (unsigned(raw[i]) << 16) | (n > 1 ? unsigned(raw[i + 1]) << 8 : 0u) | (n > 2 ? unsigned(raw[i + 2]) : 0u);
        s += kB64[(v >> 18) & 63]; s += kB64[(v >> 12) & 63];
        s += n > 1 ? kB64[(v >> 6) & 63] : '='; s += n > 2 ? kB64[v & 63] : '=';
    }
    return s;
}

inline bool decodeSamples(const char* s, float* cyc)
{
    unsigned char raw[kWavSamples * 2 + 3];
    int n = 0, bits = 0; unsigned acc = 0;
    for (; *s != '\0'; ++s)
    {
        const char c = *s;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | unsigned(v); bits += 6;
        if (bits >= 8) { bits -= 8; if (n >= kWavSamples * 2) return false; raw[n++] = (unsigned char)((acc >> bits) & 255); }
    }
    if (n != kWavSamples * 2) return false;
    for (int i = 0; i < kWavSamples; ++i) cyc[i] = float(raw[2 * i] | (raw[2 * i + 1] << 8)) / 65535.f;
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

// Fills table[kWaveSize]. On failure returns false, leaves the table untouched and (optionally) explains why.
inline bool compileWave(const char* text, float* table, std::string* err = nullptr)
{
    if (text == nullptr || std::strlen(text) < 3 || text[1] != ':' || std::strlen(text) > 900)
    {
        if (err) *err = "empty or unknown wave";
        return false;
    }
    std::vector<float> tmp(kWaveSize);

    if (text[0] == 'f')
    {
        WaveExpr ex(text + 2);
        std::string e;
        double lo = 1e300, hi = -1e300;
        std::vector<double> vals(kWaveSize);
        for (int i = 0; i < kWaveSize; ++i)
        {
            if (!ex.eval(double(i) / double(kWaveSize), vals[i], e)) { if (err) *err = e; return false; }
            vals[i] = std::min(1e12, std::max(-1e12, vals[i]));
            lo = std::min(lo, vals[i]); hi = std::max(hi, vals[i]);
        }
        const double span = hi - lo;
        for (int i = 0; i < kWaveSize; ++i)
            tmp[i] = span > 1e-9 ? float((vals[i] - lo) / span) : 0.f;
    }
    else if (text[0] == 'n' || text[0] == 'l')
    {
        std::vector<WaveNode> nodes;
        if (!parseNodes(text + 2, nodes)) { if (err) *err = "bad point list"; return false; }
        for (int i = 0; i < kWaveSize; ++i)
            tmp[i] = evalNodes(nodes, text[0] == 'n', float(i) / float(kWaveSize));
    }
    else if (text[0] == 'w')
    {
        float cyc[kWavSamples];
        if (!decodeSamples(text + 2, cyc)) { if (err) *err = "bad sample data"; return false; }
        for (int i = 0; i < kWaveSize; ++i)
        {
            // periodic cubic interpolation of the stored cycle
            const float pos = float(i) * float(kWavSamples) / float(kWaveSize);
            const int ip = int(pos);
            const float f = pos - float(ip);
            const float x0 = cyc[(ip + kWavSamples - 1) % kWavSamples], x1 = cyc[ip % kWavSamples];
            const float x2 = cyc[(ip + 1) % kWavSamples], x3 = cyc[(ip + 2) % kWavSamples];
            const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            tmp[i] = std::min(1.f, std::max(0.f, ((c3 * f + c2) * f + c1) * f + x1));
        }
    }
    else
    {
        if (err) *err = "unknown wave type";
        return false;
    }
    std::memcpy(table, tmp.data(), sizeof(float) * kWaveSize);
    return true;
}

} // namespace swsyn
