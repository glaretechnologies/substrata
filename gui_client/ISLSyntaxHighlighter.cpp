/*=====================================================================
ISLSyntaxHighlighter.cpp
------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ISLSyntaxHighlighter.h"


//#include "../StyleSheetUtils.h"

#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


ISLSyntaxHighlighter::ISLSyntaxHighlighter(QTextDocument *parent)
:	QSyntaxHighlighter(parent),
	cur_lang(Lang_Winter)
{
	HighlightingRule rule;

	QBrush function_expr_brush(Qt::blue);
	QBrush keyword_brush(Qt::darkBlue);
	QBrush string_literal_brush(Qt::darkRed);
	QBrush comment_brush(Qt::darkGreen);

	/*if(StyleSheetUtils::getCurrentTheme() == "dark")
	{
		function_expr_brush = QColor(130, 150, 255);
		keyword_brush = QColor(80, 150, 255);
		comment_brush = QColor(100, 160, 100);
	}*/

	// Winter
	{
		functionFormat.setFontItalic(true);
		functionFormat.setForeground(function_expr_brush);
		rule.pattern = QRegExp("\\b[A-Za-z0-9_]+(?=\\()");
		rule.format = functionFormat;
		winter_highlightingRules.append(rule);

		keywordFormat.setForeground(keyword_brush);
		keywordFormat.setFontWeight(QFont::Bold);
		QStringList keywordPatterns;
		keywordPatterns << "\\bvec3\\b" << "\\breal\\b" << "\\bvec2\\b" << "\\bint\\b" << "\\bbool\\b" << "\\bstruct\\b" << 
			"\\bdef\\b" << "\\blet\\b" << "\\bin\\b" << "\\bif\\b" << "\\bthen\\b" << "\\belse\\b";

		foreach (const QString &pattern, keywordPatterns) {
			rule.pattern = QRegExp(pattern);
			rule.format = keywordFormat;
			winter_highlightingRules.append(rule);
		}

		quotationFormat.setForeground(string_literal_brush);
		rule.pattern = QRegExp("\".*\"");
		rule.format = quotationFormat;
		winter_highlightingRules.append(rule);

		singleLineCommentFormat.setForeground(comment_brush);
		rule.pattern = QRegExp("#[^\n]*");
		rule.format = singleLineCommentFormat;
		winter_highlightingRules.append(rule);
	}

	// Lua
	{
		functionFormat.setFontItalic(true);
		functionFormat.setForeground(function_expr_brush);
		rule.pattern = QRegExp("\\b[A-Za-z0-9_]+(?=\\()");
		rule.format = functionFormat;
		lua_highlightingRules.append(rule);

		keywordFormat.setForeground(keyword_brush);
		keywordFormat.setFontWeight(QFont::Bold);
		QStringList keywordPatterns;
		keywordPatterns << "\\bNumber\\b" << "\\bBoolean\\b" << "\\bString\\b" << 
			"\\bfunction\\b" << "\\blocal\\b" << "\\bif\\b" << "\\belse\\b" << "\\bthen\\b";

		foreach (const QString &pattern, keywordPatterns) {
			rule.pattern = QRegExp(pattern);
			rule.format = keywordFormat;
			lua_highlightingRules.append(rule);
		}

		quotationFormat.setForeground(string_literal_brush);
		rule.pattern = QRegExp("\".*\"");
		rule.format = quotationFormat;
		lua_highlightingRules.append(rule);

		singleQuotationFormat.setForeground(string_literal_brush);
		rule.pattern = QRegExp("'.*'");
		rule.format = singleQuotationFormat;
		lua_highlightingRules.append(rule);

		singleLineCommentFormat.setForeground(comment_brush);
		rule.pattern = QRegExp("--[^\n]*");
		rule.format = singleLineCommentFormat;
		lua_highlightingRules.append(rule);
	}
}


void ISLSyntaxHighlighter::addErrorAtCharIndex(int index, int len)
{
	errors.push_back({index, len});
}


void ISLSyntaxHighlighter::doRehighlight()
{
	this->blockSignals(true);
	rehighlight();
	this->blockSignals(false);
}


void ISLSyntaxHighlighter::clearError()
{
	errors.clear();
}


void ISLSyntaxHighlighter::highlightBlock(const QString &text)
{
	const QVector<HighlightingRule>& use_rules = (cur_lang == Lang_Winter) ? winter_highlightingRules : lua_highlightingRules;

	foreach (const HighlightingRule &rule, use_rules) {
		QRegExp expression(rule.pattern);
		int index = expression.indexIn(text);
		while (index >= 0) {
			int length = expression.matchedLength();
			setFormat(index, length, rule.format);
			index = expression.indexIn(text, index + length);
		}
	}

	const int block_start_index = this->currentBlock().position();
		
	for(size_t i=0; i<errors.size(); ++i)
	{
		if(errors[i].index >= block_start_index && errors[i].index < (block_start_index + text.size()))
		{
			const int in_block_i = errors[i].index - block_start_index;

			// Underline text with a red squigly line.
			const QTextCharFormat cur_format = format(in_block_i);

			QTextCharFormat error_format = cur_format;
			error_format.setUnderlineColor(Qt::red);
			error_format.setUnderlineStyle(QTextCharFormat::WaveUnderline);

			setFormat(in_block_i, errors[i].len, error_format);
		}
	}
}
