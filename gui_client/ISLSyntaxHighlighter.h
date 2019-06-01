/*=====================================================================
ISLSyntaxHighlighter.h
----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once

#include <QtGui/QSyntaxHighlighter>

#include <QtCore/QHash>
#include <QtGui/QTextCharFormat>

/*=====================================================================
ISLSyntaxHighlighter
-------------------

=====================================================================*/
class ISLSyntaxHighlighter : public QSyntaxHighlighter
{
	//Q_OBJECT
public:
	ISLSyntaxHighlighter(QTextDocument *parent = 0);


	void showErrorAtCharIndex(int index, int len);
	void clearError();

protected:
	void highlightBlock(const QString &text);

private:
	struct HighlightingRule
	{
		QRegExp pattern;
		QTextCharFormat format;
	};
	QVector<HighlightingRule> highlightingRules;

	QTextCharFormat keywordFormat;
	QTextCharFormat classFormat;
	QTextCharFormat singleLineCommentFormat;
	QTextCharFormat quotationFormat;
	QTextCharFormat functionFormat;

	int error_index; // -1 if no error
	int error_len;
};
