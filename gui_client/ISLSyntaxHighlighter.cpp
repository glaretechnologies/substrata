/*=====================================================================
ISLSyntaxHighlighter.cpp
------------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "ISLSyntaxHighlighter.h"


//#include "../StyleSheetUtils.h"

#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


ISLSyntaxHighlighter::ISLSyntaxHighlighter(QTextDocument *parent)
:	QSyntaxHighlighter(parent),
	error_index(-1)
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

	functionFormat.setFontItalic(true);
	functionFormat.setForeground(function_expr_brush);
	rule.pattern = QRegExp("\\b[A-Za-z0-9_]+(?=\\()");
	rule.format = functionFormat;
	highlightingRules.append(rule);

	keywordFormat.setForeground(keyword_brush);
	keywordFormat.setFontWeight(QFont::Bold);
	QStringList keywordPatterns;
	keywordPatterns << "\\bvec3\\b" << "\\breal\\b" << "\\bvec2\\b" << "\\bint\\b" << "\\bbool\\b" << "\\bstruct\\b" << 
		"\\bdef\\b" << "\\blet\\b" << "\\bin\\b" << "\\bif\\b" << "\\bthen\\b" << "\\belse\\b";

	foreach (const QString &pattern, keywordPatterns) {
		rule.pattern = QRegExp(pattern);
		rule.format = keywordFormat;
		highlightingRules.append(rule);
	}

	quotationFormat.setForeground(string_literal_brush);
	rule.pattern = QRegExp("\".*\"");
	rule.format = quotationFormat;
	highlightingRules.append(rule);

	singleLineCommentFormat.setForeground(comment_brush);
	rule.pattern = QRegExp("#[^\n]*");
	rule.format = singleLineCommentFormat;
	highlightingRules.append(rule);
}


void ISLSyntaxHighlighter::showErrorAtCharIndex(int index, int len)
{
	this->error_index = index;
	this->error_len = len;

	this->blockSignals(true);
	rehighlight();
	this->blockSignals(false);
}


void ISLSyntaxHighlighter::clearError()
{
	this->error_index = -1;

	this->blockSignals(true);
	rehighlight();
	this->blockSignals(false);
}


void ISLSyntaxHighlighter::highlightBlock(const QString &text)
{
	foreach (const HighlightingRule &rule, highlightingRules) {
		QRegExp expression(rule.pattern);
		int index = expression.indexIn(text);
		while (index >= 0) {
			int length = expression.matchedLength();
			setFormat(index, length, rule.format);
			index = expression.indexIn(text, index + length);
		}
	}

	const int block_start_index = this->currentBlock().position();

	if(error_index != -1) // If there is an error to show:
	{
		if(error_index >= block_start_index && error_index < (block_start_index + text.size()))
		{
			const int in_block_i = error_index - block_start_index;

			// Underline text with a red squigly line.
			const QTextCharFormat cur_format = format(in_block_i);

			QTextCharFormat error_format = cur_format;
			error_format.setUnderlineColor(Qt::red);
			error_format.setUnderlineStyle(QTextCharFormat::WaveUnderline);

			setFormat(in_block_i, this->error_len, error_format);
		}
	}
}
