/*=====================================================================
ISLSyntaxHighlighter.h
----------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <QtGui/QSyntaxHighlighter>
#include <QtCore/QHash>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtCore5Compat/QRegExp>
#else
#include <QtCore/QRegExp>
#endif
#include <QtGui/QTextCharFormat>


/*=====================================================================
ISLSyntaxHighlighter
--------------------
Actually does Winter and Lua highlighting.
=====================================================================*/
class ISLSyntaxHighlighter : public QSyntaxHighlighter
{
	//Q_OBJECT
public:
	ISLSyntaxHighlighter(QTextDocument *parent = 0);


	void clearError(); // Does not rehighlight
	void addErrorAtCharIndex(int index, int len); // Does not rehighlight

	void doRehighlight();

	enum Lang
	{
		Lang_Winter,
		Lang_Lua
	};
	void setCurLang(Lang lang) { cur_lang = lang; }

protected:
	void highlightBlock(const QString &text);

private:
	Lang cur_lang;
	struct HighlightingRule
	{
		QRegExp pattern;
		QTextCharFormat format;
	};
	QVector<HighlightingRule> winter_highlightingRules;
	QVector<HighlightingRule> lua_highlightingRules;

	QTextCharFormat keywordFormat;
	QTextCharFormat classFormat;
	QTextCharFormat singleLineCommentFormat;
	QTextCharFormat quotationFormat;
	QTextCharFormat singleQuotationFormat;
	QTextCharFormat functionFormat;

	struct ErrorToShow
	{
		int index, len;
	};
	std::vector<ErrorToShow> errors;
};
