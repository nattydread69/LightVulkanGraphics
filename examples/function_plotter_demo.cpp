// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Function Plotter
//
// A 2D math-function plotter built entirely out of LVGUI primitives: type an expression
// of x into the text box and press Enter (or click Plot) to graph it, load scatter data
// from a CSV file to overlay against it, and export the currently-plotted curve back out
// to CSV. There is no 3D scene here at all: the graph is a PlotView, LVGUI's 2D plot
// widget. Zoom it with the wheel (Shift: X only, Ctrl: Y only), drag to pan, and
// double-click to go back to the range set in the panel -- the function is re-sampled
// over whatever is on screen, so the curve stays smooth however far in you zoom.
//
// The expression parser (recursive-descent, +-*/^, unary minus, parentheses, a fixed set
// of named functions, and the constants pi/e) is a small, self-contained piece written
// for this demo -- not a general-purpose facility the library exposes elsewhere.
//
// Widgets demonstrated:
//   TextBox (setOnSubmit) -- the expression field; Enter re-plots
//   DropDown    -- a handful of preset example expressions
//   DragValue   -- unbounded X/Y axis range editing (a Slider needs a fixed range itself,
//                  which is awkward for the thing BEING the range control)
//   Checkbox    -- auto-fit Y range to the data
//   SliderInt   -- sample count (curve resolution)
//   OpenFileDialog / SaveFileDialog -- load scatter CSV / export curve CSV
//   CollapsingSection -- groups Range/Resolution, Scatter Data, Export
//   PlotView    -- the graph: a line series for f(x) and a points series for the
//                  scatter data, with a legend (click an entry to hide/show it), a
//                  hover readout of the nearest point, and setOnViewChange() driving
//                  the re-sampling
//
// See docs/gui_usage.md for a guide to the GUI layer and docs/gui/05-widgets.md for the
// full per-widget spec.

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace lvgui = lightGraphics::ui;
namespace fs = std::filesystem;

// ============================================================================
// A minimal recursive-descent parser/evaluator for expressions of one variable, x.
// Grammar (lowest to highest precedence):
//   expression := term (('+'|'-') term)*
//   term       := unary (('*'|'/') unary)*
//   unary      := ('-'|'+') unary | power
//   power      := primary ('^' unary)?          -- right-associative, so 2^-3 works
//   primary    := number | ident ['(' expression (',' expression)* ')'] | '(' expression ')'
// Identifiers are case-insensitive. "x" is the variable; "pi"/"e" are constants; anything
// else followed by '(' is a function call, checked against a fixed table below.
// ============================================================================
namespace
{

struct ExprError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

class Expression
{
public:
	// Throws ExprError with a human-readable message on any syntax problem. On success,
	// evaluate() is safe to call immediately.
	void parse(const std::string& text)
	{
		m_tokens = tokenize(text);
		m_pos = 0;
		m_root = parseExpression(0);
		expect(TokType::End, "unexpected trailing input after the expression");
	}

	// Never throws: division by zero, sqrt of a negative number, etc. all flow through
	// as IEEE Inf/NaN (the same way the underlying <cmath> calls already behave), which
	// PlotView draws as a gap in the curve rather than a hard failure.
	float evaluate(float x) const { return static_cast<float>(evalNode(*m_root, static_cast<double>(x))); }

private:
	enum class TokType { Number, Ident, Plus, Minus, Star, Slash, Caret, Comma, LParen, RParen, End };
	struct Token
	{
		TokType type;
		std::string text;
		double value = 0.0;
	};

	struct Node
	{
		enum class Kind { Number, Variable, Unary, Binary, Call } kind;
		double number = 0.0;
		char op = 0; // Unary/Binary
		std::string name; // Call
		std::vector<std::unique_ptr<Node>> args; // Unary: 1, Binary: 2, Call: N
	};

	static std::vector<Token> tokenize(const std::string& s)
	{
		std::vector<Token> out;
		std::size_t i = 0;
		while (i < s.size())
		{
			char c = s[i];
			if (std::isspace(static_cast<unsigned char>(c)))
			{
				++i;
				continue;
			}
			if (std::isdigit(static_cast<unsigned char>(c)) ||
				(c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
			{
				char* end = nullptr;
				double v = std::strtod(s.c_str() + i, &end);
				std::size_t consumed = static_cast<std::size_t>(end - (s.c_str() + i));
				if (consumed == 0)
				{
					throw ExprError("invalid number near '" + s.substr(i, 8) + "'");
				}
				out.push_back({ TokType::Number, "", v });
				i += consumed;
				continue;
			}
			if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
			{
				std::size_t j = i;
				while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
				{
					++j;
				}
				out.push_back({ TokType::Ident, toLower(s.substr(i, j - i)), 0.0 });
				i = j;
				continue;
			}
			switch (c)
			{
				case '+': out.push_back({ TokType::Plus, "", 0.0 }); break;
				case '-': out.push_back({ TokType::Minus, "", 0.0 }); break;
				case '*': out.push_back({ TokType::Star, "", 0.0 }); break;
				case '/': out.push_back({ TokType::Slash, "", 0.0 }); break;
				case '^': out.push_back({ TokType::Caret, "", 0.0 }); break;
				case ',': out.push_back({ TokType::Comma, "", 0.0 }); break;
				case '(': out.push_back({ TokType::LParen, "", 0.0 }); break;
				case ')': out.push_back({ TokType::RParen, "", 0.0 }); break;
				default: throw ExprError(std::string("unexpected character '") + c + "'");
			}
			++i;
		}
		out.push_back({ TokType::End, "", 0.0 });
		return out;
	}

	const Token& peek() const { return m_tokens[m_pos]; }
	const Token& advance()
	{
		const Token& t = m_tokens[m_pos];
		if (m_pos + 1 < m_tokens.size())
		{
			++m_pos;
		}
		return t;
	}
	void expect(TokType type, const std::string& message)
	{
		if (peek().type != type)
		{
			throw ExprError(message);
		}
		advance();
	}
	// A guard against pathological input (thousands of nested parens) blowing the call
	// stack -- not a real-world formula ever needs anywhere near this depth.
	static void checkDepth(int depth)
	{
		if (depth > 64)
		{
			throw ExprError("expression nested too deeply");
		}
	}

	static std::unique_ptr<Node> makeUnary(char op, std::unique_ptr<Node> a)
	{
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::Unary;
		n->op = op;
		n->args.push_back(std::move(a));
		return n;
	}
	static std::unique_ptr<Node> makeBinary(char op, std::unique_ptr<Node> a, std::unique_ptr<Node> b)
	{
		auto n = std::make_unique<Node>();
		n->kind = Node::Kind::Binary;
		n->op = op;
		n->args.push_back(std::move(a));
		n->args.push_back(std::move(b));
		return n;
	}

	std::unique_ptr<Node> parseExpression(int depth)
	{
		checkDepth(depth);
		auto left = parseTerm(depth + 1);
		while (peek().type == TokType::Plus || peek().type == TokType::Minus)
		{
			char op = peek().type == TokType::Plus ? '+' : '-';
			advance();
			left = makeBinary(op, std::move(left), parseTerm(depth + 1));
		}
		return left;
	}
	std::unique_ptr<Node> parseTerm(int depth)
	{
		checkDepth(depth);
		auto left = parseUnary(depth + 1);
		while (peek().type == TokType::Star || peek().type == TokType::Slash)
		{
			char op = peek().type == TokType::Star ? '*' : '/';
			advance();
			left = makeBinary(op, std::move(left), parseUnary(depth + 1));
		}
		return left;
	}
	std::unique_ptr<Node> parseUnary(int depth)
	{
		checkDepth(depth);
		if (peek().type == TokType::Minus)
		{
			advance();
			return makeUnary('-', parseUnary(depth + 1));
		}
		if (peek().type == TokType::Plus)
		{
			advance();
			return parseUnary(depth + 1);
		}
		return parsePower(depth + 1);
	}
	std::unique_ptr<Node> parsePower(int depth)
	{
		checkDepth(depth);
		auto base = parsePrimary(depth + 1);
		if (peek().type == TokType::Caret)
		{
			advance();
			return makeBinary('^', std::move(base), parseUnary(depth + 1)); // right-assoc, allows 2^-3
		}
		return base;
	}
	std::unique_ptr<Node> parsePrimary(int depth)
	{
		checkDepth(depth);
		const Token t = peek();
		if (t.type == TokType::Number)
		{
			advance();
			auto n = std::make_unique<Node>();
			n->kind = Node::Kind::Number;
			n->number = t.value;
			return n;
		}
		if (t.type == TokType::LParen)
		{
			advance();
			auto inner = parseExpression(depth + 1);
			expect(TokType::RParen, "expected ')'");
			return inner;
		}
		if (t.type == TokType::Ident)
		{
			advance();
			if (peek().type == TokType::LParen)
			{
				advance();
				std::vector<std::unique_ptr<Node>> args;
				if (peek().type != TokType::RParen)
				{
					args.push_back(parseExpression(depth + 1));
					while (peek().type == TokType::Comma)
					{
						advance();
						args.push_back(parseExpression(depth + 1));
					}
				}
				expect(TokType::RParen, "expected ')' after '" + t.text + "('s arguments");
				checkArity(t.text, args.size());
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Call;
				n->name = t.text;
				n->args = std::move(args);
				return n;
			}
			if (t.text == "x")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Variable;
				return n;
			}
			if (t.text == "pi")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Number;
				n->number = M_PI;
				return n;
			}
			if (t.text == "e")
			{
				auto n = std::make_unique<Node>();
				n->kind = Node::Kind::Number;
				n->number = M_E;
				return n;
			}
			throw ExprError("unknown identifier '" + t.text + "' (did you mean a function call, e.g. '" + t.text + "(x)'?)");
		}
		throw ExprError("expected a number, '(', or a name, not end of expression");
	}

	static void checkArity(const std::string& name, std::size_t count)
	{
		static const std::unordered_map<std::string, int> kArity1 = {
			{ "sin", 1 }, { "cos", 1 }, { "tan", 1 }, { "asin", 1 }, { "acos", 1 }, { "atan", 1 },
			{ "sinh", 1 }, { "cosh", 1 }, { "tanh", 1 }, { "exp", 1 }, { "ln", 1 }, { "log", 1 },
			{ "log10", 1 }, { "sqrt", 1 }, { "abs", 1 }, { "floor", 1 }, { "ceil", 1 }, { "round", 1 },
			{ "sign", 1 },
		};
		static const std::unordered_map<std::string, int> kArity2 = {
			{ "pow", 2 }, { "min", 2 }, { "max", 2 }, { "mod", 2 }, { "atan2", 2 },
		};
		if (auto it = kArity1.find(name); it != kArity1.end())
		{
			if (count != 1) throw ExprError("'" + name + "' takes exactly 1 argument");
			return;
		}
		if (auto it = kArity2.find(name); it != kArity2.end())
		{
			if (count != 2) throw ExprError("'" + name + "' takes exactly 2 arguments");
			return;
		}
		throw ExprError("unknown function '" + name + "'");
	}

	static double evalCall(const std::string& name, const std::vector<std::unique_ptr<Node>>& args, double x)
	{
		if (args.size() == 1)
		{
			double a = evalNode(*args[0], x);
			if (name == "sin") return std::sin(a);
			if (name == "cos") return std::cos(a);
			if (name == "tan") return std::tan(a);
			if (name == "asin") return std::asin(a);
			if (name == "acos") return std::acos(a);
			if (name == "atan") return std::atan(a);
			if (name == "sinh") return std::sinh(a);
			if (name == "cosh") return std::cosh(a);
			if (name == "tanh") return std::tanh(a);
			if (name == "exp") return std::exp(a);
			if (name == "ln" || name == "log") return std::log(a);
			if (name == "log10") return std::log10(a);
			if (name == "sqrt") return std::sqrt(a);
			if (name == "abs") return std::fabs(a);
			if (name == "floor") return std::floor(a);
			if (name == "ceil") return std::ceil(a);
			if (name == "round") return std::round(a);
			if (name == "sign") return (a > 0.0) - (a < 0.0);
		}
		else if (args.size() == 2)
		{
			double a = evalNode(*args[0], x);
			double b = evalNode(*args[1], x);
			if (name == "pow") return std::pow(a, b);
			if (name == "min") return std::min(a, b);
			if (name == "max") return std::max(a, b);
			if (name == "mod") return std::fmod(a, b);
			if (name == "atan2") return std::atan2(a, b);
		}
		return std::numeric_limits<double>::quiet_NaN(); // unreachable: checkArity() already rejected this at parse time
	}

	static double evalNode(const Node& n, double x)
	{
		switch (n.kind)
		{
			case Node::Kind::Number: return n.number;
			case Node::Kind::Variable: return x;
			case Node::Kind::Unary: return -evalNode(*n.args[0], x);
			case Node::Kind::Binary:
			{
				double a = evalNode(*n.args[0], x);
				double b = evalNode(*n.args[1], x);
				switch (n.op)
				{
					case '+': return a + b;
					case '-': return a - b;
					case '*': return a * b;
					case '/': return a / b;
					case '^': return std::pow(a, b);
					default: return std::numeric_limits<double>::quiet_NaN();
				}
			}
			case Node::Kind::Call: return evalCall(n.name, n.args, x);
		}
		return std::numeric_limits<double>::quiet_NaN();
	}

	std::vector<Token> m_tokens;
	std::size_t m_pos = 0;
	std::unique_ptr<Node> m_root;
};

// All mutable plotter state, gathered in one place so the widget callbacks (built inline
// in main()) can reach it without a long lambda-capture list each.
struct PlotterState
{
	std::string exprText = "sin(x)";
	float xMin = -10.0f, xMax = 10.0f;
	float yMin = -1.5f, yMax = 1.5f;
	bool yAutoScale = true;
	int samples = 400;

	std::string scatterFile;

	lvgui::PlotView* plot = nullptr;
	int curveSeries = -1;
	int scatterSeries = -1;
	lvgui::Label* errorLabel = nullptr;
	lvgui::Label* scatterStatusLabel = nullptr;
	lvgui::Label* saveStatusLabel = nullptr;
	lvgui::DragValueT<float>* yMinDrag = nullptr;
	lvgui::DragValueT<float>* yMaxDrag = nullptr;

	void setError(const std::string& msg)
	{
		if (!errorLabel)
		{
			return;
		}
		errorLabel->setText(msg);
		if (!msg.empty())
		{
			// Label::setColor() has no "clear back to theme default" -- harmless here
			// since an empty message means no text is drawn at all regardless of colour.
			errorLabel->setColor(lvgui::Color{ 0xE0, 0x5A, 0x5A, 0xFF });
		}
	}

	// Samples the current expression across [lo, hi] into the plot's curve series.
	void sampleCurve(float lo, float hi)
	{
		if (!plot)
		{
			return;
		}
		Expression expr;
		std::vector<lvgui::Vec2> points;
		try
		{
			expr.parse(exprText);
		}
		catch (const ExprError& e)
		{
			setError(std::string("Parse error: ") + e.what());
			plot->clearSeries(curveSeries);
			return;
		}

		int n = std::clamp(samples, 2, 4000);
		points.reserve(static_cast<std::size_t>(n));
		bool anyFinite = false;
		for (int i = 0; i < n; ++i)
		{
			float t = static_cast<float>(i) / static_cast<float>(n - 1);
			float x = lo + t * (hi - lo);
			float y = expr.evaluate(x);
			points.push_back({ x, y });
			anyFinite = anyFinite || std::isfinite(y);
		}
		setError(anyFinite ? std::string() : "Function produced no finite values over this X range.");
		plot->setSeriesName(curveSeries, exprText);
		plot->setSeriesData(curveSeries, std::move(points));
	}

	// Re-samples the expression and puts the plot back on the panel's X range, with Y
	// either fitted to the data or fixed to the Y fields. Called by every control that
	// affects the plot -- expression commit, range/sample changes, auto-fit toggling --
	// and it discards any zoom/pan the user made in the plot itself.
	void replot()
	{
		sampleCurve(xMin, xMax);
		if (!plot)
		{
			return;
		}
		plot->setXRange(xMin, xMax);
		if (yAutoScale)
		{
			plot->setAutoFit(false, true);
			showFittedY();
		}
		else
		{
			plot->setYRange(yMin, yMax);
		}
	}

	// Copies the Y range the plot fitted to the data into the (disabled, bound) Y min/max
	// fields, so they show what auto-fit chose.
	void showFittedY()
	{
		if (plot && yAutoScale && !plot->hasUserView())
		{
			lvgui::PlotRange v = plot->view();
			yMin = static_cast<float>(v.yMin);
			yMax = static_cast<float>(v.yMax);
		}
	}

	// The user zoomed, panned or double-clicked the plot: the range fields follow it, and
	// the curve is re-sampled over exactly what is now on screen.
	void onViewChanged(const lvgui::PlotRange& v)
	{
		xMin = static_cast<float>(v.xMin);
		xMax = static_cast<float>(v.xMax);
		yMin = static_cast<float>(v.yMin);
		yMax = static_cast<float>(v.yMax);
		sampleCurve(xMin, xMax);
	}

	void loadScatter(const std::string& path)
	{
		std::ifstream in(path);
		if (!in)
		{
			if (scatterStatusLabel) scatterStatusLabel->setText("Could not open " + path);
			return;
		}
		std::vector<lvgui::Vec2> points;
		std::string line;
		while (std::getline(in, line))
		{
			// Accept "x,y" or "x y"; skip blank lines and anything that isn't two
			// parseable numbers (a header row like "x,y" harmlessly fails to parse and
			// is skipped the same way).
			std::replace(line.begin(), line.end(), ',', ' ');
			std::istringstream iss(line);
			float x, y;
			if (iss >> x >> y)
			{
				points.push_back({ x, y });
			}
		}
		scatterFile = fs::path(path).filename().string();
		if (plot)
		{
			plot->setSeriesData(scatterSeries, std::move(points));
			plot->setSeriesName(scatterSeries, scatterFile);
		}
		if (scatterStatusLabel)
		{
			scatterStatusLabel->setText(std::to_string(plot ? plot->seriesData(scatterSeries).size() : 0) +
				" point(s) loaded from " + scatterFile);
		}
		showFittedY();
	}

	void clearScatter()
	{
		if (plot)
		{
			plot->clearSeries(scatterSeries);
			plot->setSeriesName(scatterSeries, "scatter");
		}
		scatterFile.clear();
		if (scatterStatusLabel) scatterStatusLabel->setText("No scatter data loaded.");
		showFittedY();
	}

	void saveCurve(const std::string& path)
	{
		std::ofstream out(path, std::ios::trunc);
		if (!out)
		{
			if (saveStatusLabel) saveStatusLabel->setText("Could not write " + path);
			return;
		}
		out << "x,y\n";
		std::size_t written = 0;
		if (plot)
		{
			for (const lvgui::Vec2& p : plot->seriesData(curveSeries))
			{
				if (std::isfinite(p.x) && std::isfinite(p.y))
				{
					out << p.x << ',' << p.y << '\n';
					++written;
				}
			}
		}
		if (saveStatusLabel)
		{
			saveStatusLabel->setText("Saved " + std::to_string(written) + " point(s) to " +
				fs::path(path).filename().string());
		}
	}
};

const std::vector<std::string> kExampleLabels = {
	"sin(x)", "cos(x) * x", "x^2 - 3", "exp(-x^2 / 8) * 10", "1 / x", "sqrt(abs(x))", "tan(x)",
};

} // namespace

int main()
{
	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 840, "Function Plotter");
		app.setKeyboardCameraEnabled(false); // this demo has no 3D scene to fly a camera through
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[function-plotter] hasGui() is false -- nothing to show without the GUI" << std::endl;
			app.run();
			return 0;
		}

		auto& gui = app.gui();

		const fs::path dataDir = fs::path(__FILE__).parent_path() / "plotter_data";
		std::error_code ec;
		fs::create_directories(dataDir, ec); // best-effort; the dialogs work on an empty/missing dir too

		PlotterState state;

		auto openDialog = std::make_unique<lvgui::OpenFileDialog>(gui, dataDir.string(), ".csv");
		openDialog->setOnConfirm([&state](const std::string& path) { state.loadScatter(path); });

		auto saveDialog = std::make_unique<lvgui::SaveFileDialog>(gui, dataDir.string(), ".csv");
		saveDialog->setOnConfirm([&state](const std::string& path) { state.saveCurve(path); });

		{
			auto file = gui.menuBar().addMenu("File");
			file.addItem("Load scatter...", [&openDialog] { openDialog->open(); }, "Ctrl+O");
			file.addItem("Save curve...", [&saveDialog] { saveDialog->open(); }, "Ctrl+S");
		}

		auto* panel = gui.createPanel("Function Plotter", { 20.0f, 40.0f, 480.0f, 760.0f });
		panel->setPersistenceId("function-plotter-main");

		panel->add<lvgui::Label>("Function Plotter")->setHeading(true);
		panel->add<lvgui::Label>(
			"Functions: sin cos tan asin acos atan sinh cosh tanh exp ln log log10 sqrt abs "
			"floor ceil round sign, pow(a,b) min(a,b) max(a,b) mod(a,b) atan2(y,x); constants pi, e.")
			->setWordWrap(true);
		panel->add<lvgui::Spacer>(4.0f);

		auto* plot = panel->add<lvgui::PlotView>();
		plot->setHeight(340.0f);
		plot->setXLabel("x");
		plot->setYLabel("f(x)");
		state.plot = plot;
		state.curveSeries = plot->addSeries(state.exprText, lvgui::PlotStyle::Line);
		state.scatterSeries = plot->addSeries("scatter", lvgui::PlotStyle::Points);
		plot->setOnViewChange([&state](const lvgui::PlotRange& v) { state.onViewChanged(v); });

		auto* exprBox = panel->add<lvgui::TextBox>("f(x) =", state.exprText);
		exprBox->setPlaceholder("e.g. sin(x) * x");
		exprBox->bind(&state.exprText);
		exprBox->setTooltip("Press Enter to plot. Implicit multiplication isn't supported -- write 2*x, not 2x.");
		exprBox->setOnSubmit([&state](std::string_view) { state.replot(); });

		auto* controlRow = panel->add<lvgui::Row>();
		auto* plotButton = controlRow->add<lvgui::Button>("Plot");
		plotButton->setOnClick([&state] { state.replot(); });
		auto* examplesDropdown = controlRow->add<lvgui::DropDown>("", kExampleLabels, 0);
		examplesDropdown->setOnChange([&state, exprBox](int idx) {
			state.exprText = kExampleLabels[static_cast<std::size_t>(idx)];
			exprBox->setText(state.exprText, false);
			state.replot();
		});

		state.errorLabel = panel->add<lvgui::Label>("");
		state.errorLabel->setWordWrap(true);

		auto* shortcutHintLabel = panel->add<lvgui::Label>(
			"Keyboard (while not editing f(x)): Left/Right = prev/next example.");
		shortcutHintLabel->setWordWrap(true);
		shortcutHintLabel->setColor(lvgui::Color{ 0x9A, 0xA3, 0xAF, 0xFF });

		auto* rangeSection = panel->add<lvgui::CollapsingSection>("Range & Resolution", true);
		auto* xRangeRow = rangeSection->add<lvgui::Row>();
		auto* xMinDrag = xRangeRow->add<lvgui::DragValueT<float>>("X min", state.xMin, 0.1f);
		xMinDrag->setFormat("%.2f");
		xMinDrag->bind(&state.xMin);
		xMinDrag->setOnChange([&state](float) { state.replot(); });
		auto* xMaxDrag = xRangeRow->add<lvgui::DragValueT<float>>("X max", state.xMax, 0.1f);
		xMaxDrag->setFormat("%.2f");
		xMaxDrag->bind(&state.xMax);
		xMaxDrag->setOnChange([&state](float) { state.replot(); });

		static bool autoScaleState = state.yAutoScale;
		auto* autoScaleCheckbox = rangeSection->add<lvgui::Checkbox>("Auto-fit Y range", autoScaleState);
		autoScaleCheckbox->bind(&autoScaleState);

		auto* yRangeRow = rangeSection->add<lvgui::Row>();
		auto* yMinDrag = yRangeRow->add<lvgui::DragValueT<float>>("Y min", state.yMin, 0.1f);
		yMinDrag->setFormat("%.2f");
		yMinDrag->setEnabled(false);
		auto* yMaxDrag = yRangeRow->add<lvgui::DragValueT<float>>("Y max", state.yMax, 0.1f);
		yMaxDrag->setFormat("%.2f");
		yMaxDrag->setEnabled(false);
		state.yMinDrag = yMinDrag;
		state.yMaxDrag = yMaxDrag;

		// Auto-fit and the manual Y fields are mutually exclusive: flipping the checkbox
		// enables/disables the fields it would otherwise fight with, and either an edit or
		// a re-fit needs the SAME replot() to actually reach the plot.
		autoScaleCheckbox->setOnChange([&state, yMinDrag, yMaxDrag](bool on) {
			state.yAutoScale = on;
			yMinDrag->setEnabled(!on);
			yMaxDrag->setEnabled(!on);
			state.replot();
		});
		yMinDrag->bind(&state.yMin);
		yMinDrag->setOnChange([&state](float) { if (!state.yAutoScale) state.replot(); });
		yMaxDrag->bind(&state.yMax);
		yMaxDrag->setOnChange([&state](float) { if (!state.yAutoScale) state.replot(); });

		auto* samplesSlider = rangeSection->add<lvgui::SliderInt>("Samples", 20, 4000, state.samples);
		samplesSlider->setScale(lvgui::SliderScale::Logarithmic);
		samplesSlider->setOnChange([&state](int v) { state.samples = v; state.replot(); });

		auto* scatterSection = panel->add<lvgui::CollapsingSection>("Scatter Data", true);
		auto* scatterRow = scatterSection->add<lvgui::Row>();
		auto* loadScatterButton = scatterRow->add<lvgui::Button>("Load scatter...");
		loadScatterButton->setOnClick([&openDialog] { openDialog->open(); });
		auto* clearScatterButton = scatterRow->add<lvgui::Button>("Clear scatter");
		clearScatterButton->setOnClick([&state] { state.clearScatter(); });
		state.scatterStatusLabel = scatterSection->add<lvgui::Label>("No scatter data loaded.");
		state.scatterStatusLabel->setWordWrap(true);

		auto* exportSection = panel->add<lvgui::CollapsingSection>("Export", true);
		auto* saveCurveButton = exportSection->add<lvgui::Button>("Save curve as CSV...");
		saveCurveButton->setOnClick([&saveDialog] { saveDialog->open(); });
		state.saveStatusLabel = exportSection->add<lvgui::Label>("");
		state.saveStatusLabel->setWordWrap(true);

		// First plot, now that every widget it might touch (error label, Y-range drags)
		// already exists.
		state.replot();

		// Global keyboard shortcuts, polled from the key queue the same way gui_demo polls
		// for its right-click context menu -- MenuBar's own shortcutHint text is display-
		// only (see its header comment), so this is what actually makes Ctrl+O/Ctrl+S do
		// something. Gated on !wantsKeyboard() (true while the expression TextBox is
		// focused), so Left/Right here never steals caret movement from it, and Ctrl+O/
		// Ctrl+S never fire mid-edit either -- release focus (click elsewhere, or Enter to
		// submit) first. This is also this demo's most reliable path for scripted testing:
		// `xdotool key Right` cycles the Examples dropdown where a synthetic click on a
		// specific popup row is not.
		app.setUpdateCallback([&gui, &openDialog, &saveDialog, examplesDropdown](float) {
			if (gui.wantsKeyboard())
			{
				return;
			}
			for (const lvgui::KeyEvent& ev : gui.input().keyQueue)
			{
				if (!ev.pressed || ev.repeat)
				{
					continue;
				}
				if ((ev.mods & lvgui::Mod::Ctrl) && ev.key == lvgui::Key::O)
				{
					openDialog->open();
				}
				else if ((ev.mods & lvgui::Mod::Ctrl) && ev.key == lvgui::Key::S)
				{
					saveDialog->open();
				}
				else if (ev.key == lvgui::Key::Right || ev.key == lvgui::Key::Left)
				{
					int count = static_cast<int>(kExampleLabels.size());
					int dir = (ev.key == lvgui::Key::Right) ? 1 : -1;
					int next = (examplesDropdown->selectedIndex() + dir + count) % count;
					examplesDropdown->setSelectedIndex(next, true); // fires setOnChange -> replot()
				}
			}
		});

		app.run();
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}
