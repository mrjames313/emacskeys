/**************************************************************************
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "emacskeyshandler.h"
//
// ATTENTION:
//
// 1 Please do not add any direct dependencies to other Qt Creator code here.
//   Instead emit signals and let the EmacsKeysPlugin channel the information to
//   Qt Creator. The idea is to keep this keyfile here in a "clean" state that
//   allows easy reuse with any QTextEdit or QPlainTextEdit derived class.
//
// 2 There are a few auto tests located in ../../../tests/auto/emacsKeys.
//   Commands that are covered there are marked as "// tested" below.
//
// 3 Some conventions:
//
//   Use 1 based line numbers and 0 based column numbers. Even though
//   the 1 based line are not nice it matches vim's and QTextEdit's 'line'
//   concepts.
//
//   Do not pass QTextCursor etc around unless really needed. Convert
//   early to  line/column.
//
//   There is always a "current" cursor (m_tc). A current "region of interest"
//   spans between m_anchor (== anchor()) and  m_tc.position() (== position())
//   The value of m_tc.anchor() is not used.
//

#include <utils/qtcassert.h>


#include <QDebug>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QTextStream>
#include <QtAlgorithms>
#include <QStack>

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QClipboard>

#include "markring.h"
#include "killring.h"

#define DEBUG_GENERAL 0
#if DEBUG_GENERAL
#   define GENERAL_DEBUG(s) qDebug() << s
#else
#   define GENERAL_DEBUG(s)
#endif

#define DEBUG_KEY  0
#if DEBUG_KEY
#   define KEY_DEBUG(s) qDebug() << s
#else
#   define KEY_DEBUG(s)
#endif

//#define DEBUG_UNDO  1
#if DEBUG_UNDO
#   define UNDO_DEBUG(s) qDebug() << << m_tc.document()->revision() << s
#else
#   define UNDO_DEBUG(s)
#endif

/* Working on better integration with QT existing framework for region and mark */
#define NEW_REGION 1


using namespace Utils;

namespace EmacsKeys {
namespace Internal {

///////////////////////////////////////////////////////////////////////
//
// EmacsKeysHandler
//
///////////////////////////////////////////////////////////////////////

// MRJ - get rid of these, just use fully qualified names
#define StartOfLine     QTextCursor::StartOfLine
#define EndOfLine       QTextCursor::EndOfLine
#define NextCharacter   QTextCursor::NextCharacter
#define MoveAnchor      QTextCursor::MoveAnchor
#define KeepAnchor      QTextCursor::KeepAnchor
#define Up              QTextCursor::Up
#define Down            QTextCursor::Down
#define Right           QTextCursor::Right
#define Left            QTextCursor::Left
#define EndOfDocument   QTextCursor::End
#define StartOfDocument QTextCursor::Start
#define MoveMode        QTextCursor::MoveMode

#define EDITOR(s) (m_textedit ? m_textedit->s : m_plaintextedit->s)
#define EDITOR_WIDGET m_textedit ? qobject_cast<QWidget*>(m_textedit) : qobject_cast<QWidget*>(m_plaintextedit)

const int ParagraphSeparator = 0x00002029;

using namespace Qt;


QDebug &operator<<(QDebug &ts, const QList<QTextEdit::ExtraSelection> &sels)
{
		foreach (QTextEdit::ExtraSelection sel, sels)
				ts << "SEL: " << sel.cursor.anchor() << sel.cursor.position();
		return ts;
}

QString quoteUnprintable(const QString &ba)
{
		QString res;
		for (int i = 0, n = ba.size(); i != n; ++i) {
				QChar c = ba.at(i);
				if (c.isPrint())
						res += c;
				else
						res += QString("\\x%1").arg(c.unicode(), 2, 16);
		}
		return res;
}

enum EventResult
{
		EventHandled,
		EventUnhandled,
		EventPassedToCore
};

class EmacsKeysHandler::Private
{
public:
		Private(EmacsKeysHandler *parent, QWidget *widget);

		EventResult handleEvent(QKeyEvent *ev);
		bool wantsOverride(QKeyEvent *ev);

		void installEventFilter();
		void setupWidget();
		void restoreWidget();

		friend class EmacsKeysHandler;

		void init();
		bool exactMatch(const QKeySequence& binding, const QKeySequence& keySequence);

	void yankPop(QWidget* view);
	void setMark();
	void exchangeDotAndMark();
	void popToMark(MoveMode move_mode);
	void copy();
	void cut();
	void yank();
	void killLine(bool augmentLine);
	void killWord();
	void backwardKillWord();

	void removeWhitespace();

	bool atEndOfLine() const
	{ return m_tc.atBlockEnd() && m_tc.block().length() > 1; }

	// all zero-based counting
	int cursorLineOnScreen() const;
	int linesOnScreen() const;
	int cursorLineInDocument() const;
	int linesInDocument() const;
	void scrollToLineInDocument(int line);
	void scrollUp(int count);
	void scrollDown(int count) { scrollUp(-count); }

	void moveToNextWord(MoveMode move_mode) { m_tc.movePosition(QTextCursor::NextWord, move_mode); }
	void moveToPreviousWord(MoveMode move_mode) { m_tc.movePosition(QTextCursor::PreviousWord, move_mode); }
	void moveToEndOfDocument(MoveMode move_mode) { m_tc.movePosition(EndOfDocument, move_mode); }
	void moveToStartOfLine(MoveMode move_mode) { m_tc.movePosition(QTextCursor::StartOfLine, move_mode); }
	void moveToEndOfLine(MoveMode move_mode) { m_tc.movePosition(QTextCursor::EndOfLine, move_mode); }
	void moveUp(int n, MoveMode move_mode) { m_tc.movePosition(QTextCursor::Up, move_mode, n); }
	void moveDown(int n, MoveMode move_mode) { m_tc.movePosition(Down, move_mode, n); }
	void moveRight(int n, MoveMode move_mode) { m_tc.movePosition(Right, move_mode, n); }
	void moveLeft(int n, MoveMode move_mode) { m_tc.movePosition(Left, move_mode, n); }

	void setAnchor() { m_anchor = m_tc.position(); }
	void setAnchor(int position) { m_anchor = position; }
	void setPosition(int position) { m_tc.setPosition(position, MoveAnchor); }

	QWidget *editor() const;
	QChar characterAtCursor() const
	{ return m_tc.document()->characterAt(m_tc.position()); }
	void beginEditBlock() { UNDO_DEBUG("BEGIN EDIT BLOCK"); m_tc.beginEditBlock(); }
	void endEditBlock() { UNDO_DEBUG("END EDIT BLOCK"); m_tc.endEditBlock(); }

	bool isMovementCommand(QKeySequence keySequence);
	bool isEmacsCommand(QKeySequence keySequence);

	/* Key bindings, see init for defs */
	QKeySequence ks_moveDown;
	QKeySequence ks_moveUp;
	QKeySequence ks_moveStartLine;
	QKeySequence ks_moveEndLine;
	QKeySequence ks_moveLeft;
	QKeySequence ks_moveRight;
	QKeySequence ks_moveWordLeft;
	QKeySequence ks_moveWordRight;
	QKeySequence ks_moveDocStart;
	QKeySequence ks_moveDocEnd;
	QKeySequence ks_movePageDown;
	QKeySequence ks_movePageUp;
	QKeySequence ks_moveRecenter;

	QKeySequence ks_killWord;
	QKeySequence ks_backKillWord;
	QKeySequence ks_deleteChar;
	QKeySequence ks_setMark;
	QKeySequence ks_setMark2;
	QKeySequence ks_killLine;
	QKeySequence ks_yank;
	QKeySequence ks_yankPop;
	QKeySequence ks_cut;
	QKeySequence ks_copy;
	QKeySequence ks_popToMark;
	QKeySequence ks_exchangeDotAndMark;
	QKeySequence ks_removeWhitespace;

	QKeySequence ks_cancelMark; // MRJ - special - does this do anything?

public:
	QTextEdit *m_textedit;
	QPlainTextEdit *m_plaintextedit;
	bool m_wasReadOnly; // saves read-only state of document

	EmacsKeysHandler *q;
	QTextCursor m_tc;
	int m_anchor;
	// MRJ - need to check that there is a different mark ring kept for each buffer...
	MarkRing markRing;

	QString removeSelectedText();
	int anchor() const { return m_anchor; }
	int position() const { return m_tc.position(); }
	QString selectedText() const;

	// for restoring cursor position
	int m_savedYankPosition;

	int m_cursorWidth;

	int yankEndPosition;
	int yankStartPosition;
};


EmacsKeysHandler::Private::Private(EmacsKeysHandler *parent, QWidget *widget)
{
	q = parent;
	m_textedit = qobject_cast<QTextEdit *>(widget);
	m_plaintextedit = qobject_cast<QPlainTextEdit *>(widget);
	init();
}

void EmacsKeysHandler::Private::init()
{
	m_anchor = 0;
	m_savedYankPosition = 0;
	m_cursorWidth = EDITOR(cursorWidth());

	// MRJ - for now... do not use Ctrl-X
	ks_moveDown = QKeySequence(Qt::CTRL + Qt::Key_N);
	ks_moveUp = QKeySequence(Qt::CTRL + Qt::Key_P);
	ks_moveStartLine = QKeySequence(Qt::CTRL + Qt::Key_A);
	ks_moveEndLine = QKeySequence(Qt::CTRL + Qt::Key_E);
	ks_moveLeft = QKeySequence(Qt::CTRL + Qt::Key_B);
	ks_moveRight = QKeySequence(Qt::CTRL + Qt::Key_F);
	ks_moveWordLeft = QKeySequence(Qt::ALT + Qt::Key_B);
	ks_moveWordRight = QKeySequence(Qt::ALT + Qt::Key_F);
	ks_moveDocStart = QKeySequence(Qt::ALT + Qt::SHIFT + Qt::Key_Less);
	ks_moveDocEnd = QKeySequence(Qt::ALT + Qt::SHIFT + Qt::Key_Greater);
	ks_movePageUp = QKeySequence(Qt::CTRL + Qt::Key_V);
	ks_movePageDown = QKeySequence(Qt::ALT + Qt::Key_V);
	ks_moveRecenter = QKeySequence(Qt::CTRL + Qt::Key_J);

	ks_killWord = QKeySequence(Qt::ALT + Qt::Key_D);
	ks_backKillWord = QKeySequence(Qt::CTRL + Qt::Key_Backspace);
	ks_deleteChar = QKeySequence(Qt::CTRL + Qt::Key_D);
	ks_setMark = QKeySequence(Qt::CTRL + Qt::Key_Space);
	ks_setMark2 = QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_At);
	ks_killLine = QKeySequence(Qt::CTRL + Qt::Key_K);
	ks_yank = QKeySequence(Qt::CTRL + Qt::Key_Y);
	ks_yankPop = QKeySequence(Qt::ALT + Qt::Key_Y);
	ks_cut = QKeySequence(Qt::CTRL + Qt::Key_W);
	ks_copy = QKeySequence(Qt::ALT + Qt::Key_W);
	// MRJ - can't do multi-key sequences right now, figure out why... this whole structure is convoluted, can make better
	//ks_popToMark = QKeySequence(Qt::CTRL + Qt::Key_U, Qt::CTRL + Qt::Key_Space);
	ks_popToMark = QKeySequence(Qt::CTRL + Qt::Key_M);
	// MRJ - next is problematic... replace temporarily
	//		ks_exchangeDotAndMark = QKeySequence(Qt::CTRL + Qt::Key_X, Qt::Key_X);
	ks_exchangeDotAndMark = QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_M);
	ks_removeWhitespace = QKeySequence(Qt::ALT + Qt::Key_Space);
	ks_cancelMark = QKeySequence(Qt::CTRL + Qt::Key_G);
}

bool EmacsKeysHandler::Private::isMovementCommand(QKeySequence keySequence)
{
	return (exactMatch(ks_moveDown, keySequence) or
					exactMatch(ks_moveUp, keySequence) or
					exactMatch(ks_moveStartLine, keySequence) or
					exactMatch(ks_moveEndLine, keySequence) or
					exactMatch(ks_moveLeft, keySequence) or
					exactMatch(ks_moveRight, keySequence) or
					exactMatch(ks_moveWordLeft, keySequence) or
					exactMatch(ks_moveWordRight, keySequence) or
					exactMatch(ks_moveDocStart, keySequence) or
					exactMatch(ks_moveDocEnd, keySequence) or
					exactMatch(ks_movePageDown, keySequence) or
					exactMatch(ks_movePageUp, keySequence) or
					exactMatch(ks_exchangeDotAndMark, keySequence) or /* Because it selects a region */
					exactMatch(ks_moveRecenter, keySequence));
}

bool EmacsKeysHandler::Private::isEmacsCommand(QKeySequence keySequence)
{
	return (exactMatch(ks_moveDown, keySequence) or
					exactMatch(ks_moveUp, keySequence) or
					exactMatch(ks_moveStartLine, keySequence) or
					exactMatch(ks_moveEndLine, keySequence) or
					exactMatch(ks_moveLeft, keySequence) or
					exactMatch(ks_moveRight, keySequence) or
					exactMatch(ks_moveWordLeft, keySequence) or
					exactMatch(ks_moveWordRight, keySequence) or
					exactMatch(ks_moveDocStart, keySequence) or
					exactMatch(ks_moveDocEnd, keySequence) or
					exactMatch(ks_movePageDown, keySequence) or
					exactMatch(ks_movePageUp, keySequence) or
					exactMatch(ks_moveRecenter, keySequence) or
					exactMatch(ks_killWord, keySequence) or
					exactMatch(ks_backKillWord, keySequence) or
					exactMatch(ks_deleteChar, keySequence) or
					exactMatch(ks_setMark, keySequence) or
					exactMatch(ks_setMark2, keySequence) or
					exactMatch(ks_killLine, keySequence) or
					exactMatch(ks_yank, keySequence) or
					exactMatch(ks_yankPop, keySequence) or
					exactMatch(ks_cut, keySequence) or
					exactMatch(ks_copy, keySequence) or
					exactMatch(ks_popToMark, keySequence) or
					exactMatch(ks_removeWhitespace, keySequence) or
					exactMatch(ks_cancelMark, keySequence));
}

bool EmacsKeysHandler::Private::wantsOverride(QKeyEvent *ev)
{
		const int key = ev->key();
		KEY_DEBUG("  Wants override ?" << key);

		/* Never override Esc */
		if (key == Key_Escape) {
			return false;
		}

		QKeySequence keySequence(ev->key() + ev->modifiers());
		if(isEmacsCommand(keySequence)) {
			KEY_DEBUG("  Not passing key sequence");
			return true;
		}

		// Let other shortcuts trigger to qt-specified...
		return false;
}

bool EmacsKeysHandler::Private::exactMatch(const QKeySequence& binding, const QKeySequence& keySequence)
{
		return binding.matches(keySequence) == QKeySequence::ExactMatch;
}


void EmacsKeysHandler::Private::yankPop(QWidget* view)
{
	GENERAL_DEBUG("yankPop called ");
	if (KillRing::instance()->currentYankView() != view) {
		GENERAL_DEBUG("the last previous yank was not in this view");
		// generate beep and return
		QApplication::beep();
		return;
	}

	int position = m_tc.position();
	if (position != yankEndPosition) {
		GENERAL_DEBUG("Cursor has been moved in the meantime");
		GENERAL_DEBUG("yank end position " << yankEndPosition);
		QApplication::beep();
		return;
	}

	QString next(KillRing::instance()->next());
	if (!next.isEmpty()) {
		GENERAL_DEBUG("yanking " << next);
		beginEditBlock();
		m_tc.setPosition(yankStartPosition, KeepAnchor);
		m_tc.removeSelectedText();
		m_tc.insertText(next);
		yankEndPosition = m_tc.position();
		endEditBlock();
	}
	else {
		GENERAL_DEBUG("killring empty");
		QApplication::beep();
	}
}


void EmacsKeysHandler::Private::setMark()
{
	GENERAL_DEBUG("set mark");
	m_tc.clearSelection();
	Mark mark(markRing.getMostRecentMark());
	if(mark.position == m_tc.position()) { // toggle mark
		markRing.toggleActive();
	} else {
		markRing.addMark(m_tc.position());
	}
}


void EmacsKeysHandler::Private::exchangeDotAndMark()
{
	GENERAL_DEBUG("exchange point and mark");
	Mark mark(markRing.getMostRecentMark()); // Can you cycle through mark ring?
	if (mark.valid) {
		GENERAL_DEBUG("  going to position " << mark.position);
		int position = m_tc.position();
		markRing.addMark(position);
		m_tc.setPosition(mark.position, KeepAnchor);
	}
	else {
		QApplication::beep();
	}
}

void EmacsKeysHandler::Private::popToMark(MoveMode move_mode)
{
	GENERAL_DEBUG("pop mark");
	Mark mark(markRing.getPreviousMark());
	if (mark.valid) {
		GENERAL_DEBUG("  going to position " << mark.position);
		m_tc.setPosition(mark.position, move_mode);
	}
	else {
		QApplication::beep();
	}
}

void EmacsKeysHandler::Private::copy()
{
	GENERAL_DEBUG("emacs copy");
#if NEW_REGION
	if(m_tc.hasSelection()) {
		beginEditBlock();
		QApplication::clipboard()->setText(m_tc.selectedText());
		m_tc.clearSelection();
		endEditBlock();
	} else {
		QApplication::beep();
	}
#else
	Mark mark(markRing.getMostRecentMark());
	if (mark.valid) {
		beginEditBlock();
		int position = m_tc.position();
		m_tc.setPosition(mark.position, KeepAnchor);
		QApplication::clipboard()->setText(m_tc.selectedText());
		m_tc.clearSelection();
		m_tc.setPosition(position);
		endEditBlock();
	}
	else {
		QApplication::beep();
	}
#endif
}

void EmacsKeysHandler::Private::cut()
{
	GENERAL_DEBUG("emacs cut");
#if NEW_REGION
	if(m_tc.hasSelection()) {
		beginEditBlock();
		QApplication::clipboard()->setText(m_tc.selectedText());
		m_tc.removeSelectedText();
		endEditBlock();
	} else {
		QApplication::beep();
	}
#else
	Mark mark(markRing.getMostRecentMark());
	if (mark.valid) {
		beginEditBlock();
		m_tc.setPosition(mark.position, KeepAnchor);
		QApplication::clipboard()->setText(m_tc.selectedText());
		m_tc.removeSelectedText();
		endEditBlock();
	}
	else {
		QApplication::beep();
	}
#endif
}

void EmacsKeysHandler::Private::yank()
{
	GENERAL_DEBUG("emacs yank");
	int position = m_tc.position();
	yankStartPosition = position;
	EDITOR(paste()); // MRJ - want to use the clipboard?
	yankEndPosition = m_tc.position();
	if (position != yankEndPosition) {
		KillRing::instance()->setCurrentYankView(EDITOR_WIDGET);
	}
}

void EmacsKeysHandler::Private::killLine(bool augmentLine)
{
	GENERAL_DEBUG("kill line" << augmentLine);
	beginEditBlock();
	int position = m_tc.position();
	GENERAL_DEBUG("current position " << position);
	m_tc.movePosition(EndOfLine, KeepAnchor);

	if (position == m_tc.position()) {
			GENERAL_DEBUG("at line end");
			m_tc.movePosition(NextCharacter, KeepAnchor);
	}
	GENERAL_DEBUG("invoke cut");
	if(augmentLine) {
		QApplication::clipboard()->setText(
			QApplication::clipboard()->text() + m_tc.selectedText());
	} else {
		QApplication::clipboard()->setText(m_tc.selectedText());
	}
	m_tc.removeSelectedText();

	endEditBlock();
}

void EmacsKeysHandler::Private::killWord()
{
	GENERAL_DEBUG("kill word");
	int position = m_tc.position();
	GENERAL_DEBUG("current position " << position);
	beginEditBlock();
	m_tc.movePosition(QTextCursor::NextWord, KeepAnchor);
	if (position != m_tc.position()) {
			GENERAL_DEBUG("invoke cut");
			QApplication::clipboard()->setText(m_tc.selectedText());
			m_tc.removeSelectedText();
	} else {
			QApplication::beep();
	}
	endEditBlock();
}

/* Expected behavior:
 *   only do something if in whitespace, or on the first character which is preceeded by whitespace (and that whitespace is not a newline)
 *     do: find start and end of target whitespace: these will never go to previous or next lines
 *     remove all that text, then add one space
 *
 */

void EmacsKeysHandler::Private::removeWhitespace()
{
	GENERAL_DEBUG("remove whitespace");
	int orig_line = m_tc.block().blockNumber();
	m_tc.movePosition(QTextCursor::PreviousCharacter, MoveAnchor); /* Detect case when we are on a word at start of the line */
	int prev_char_line = m_tc.block().blockNumber();
	m_tc.movePosition(QTextCursor::NextCharacter, MoveAnchor);
	if(not m_tc.document()->characterAt(m_tc.position()).isSpace() and
					(not m_tc.document()->characterAt(m_tc.position() - 1).isSpace() or orig_line != prev_char_line)) {
		GENERAL_DEBUG("  RW:  not on whitespace");
		return;
	}

	beginEditBlock();

	/* Find end of prev word, or start of line */
	m_tc.movePosition(QTextCursor::PreviousWord, MoveAnchor);
	m_tc.movePosition(QTextCursor::EndOfWord, MoveAnchor);
	GENERAL_DEBUG("  RW (after prev moves) orig line:" << orig_line << " and current " << m_tc.block().blockNumber());

	if(m_tc.block().blockNumber() != orig_line) {
		m_tc.movePosition(QTextCursor::NextBlock, MoveAnchor);
		GENERAL_DEBUG("  RW (after next block) " << m_tc.block().blockNumber());
	}

	/* Find start of next word or end of line */
	m_tc.movePosition(QTextCursor::NextWord, KeepAnchor);
	GENERAL_DEBUG("  RW (after next moves) orig line:" << orig_line << " and current " << m_tc.block().blockNumber());
	if(m_tc.block().blockNumber() != orig_line) {
		m_tc.movePosition(QTextCursor::PreviousBlock, KeepAnchor);
		m_tc.movePosition(QTextCursor::EndOfBlock, KeepAnchor);
		GENERAL_DEBUG("  RW (after prev block) " << m_tc.block().blockNumber());
	}

	m_tc.removeSelectedText();
	m_tc.insertText(" ");
	endEditBlock();
}

void EmacsKeysHandler::Private::backwardKillWord()
{
	GENERAL_DEBUG("backwards kill word");
	int position = m_tc.position();
	GENERAL_DEBUG("current position " << position);
	beginEditBlock();
	m_tc.movePosition(QTextCursor::PreviousWord, KeepAnchor);
	if (position != m_tc.position()) {
			GENERAL_DEBUG("invoke cut");
			QApplication::clipboard()->setText(m_tc.selectedText());
			m_tc.removeSelectedText();
	} else {
			QApplication::beep();
	}
	endEditBlock();
}


EventResult EmacsKeysHandler::Private::handleEvent(QKeyEvent *ev)
{
		int key = ev->key();
		const int mods = ev->modifiers();
		QKeySequence keySequence(ev->key() + ev->modifiers());

		static QKeySequence lastKeySequence = keySequence;

//		static bool onlyMovementSinceMark = false;

		GENERAL_DEBUG("sequence: " << keySequence);

		if (key == Key_Shift || key == Key_Alt || key == Key_Control
						|| key == Key_Alt || key == Key_AltGr || key == Key_Meta)
		{
				KEY_DEBUG("PLAIN MODIFIER");
				return EventUnhandled;
		}

		// Fake "End of line"
		m_tc = EDITOR(textCursor());

		m_tc.setVisualNavigation(true);

		// MRJ - don't think this code does anything...
		if ((mods & Qt::ControlModifier) != 0) {
				key += 256;
				key += 32; // make it lower case
		} else if (key >= Key_A && key <= Key_Z && (mods & Qt::ShiftModifier) == 0) {
				key += 32;
		}

		Mark mark(markRing.getMostRecentMark());
		MoveMode move_mode = QTextCursor::MoveAnchor;
		if(mark.active) {
			move_mode = QTextCursor::KeepAnchor;
		}

		EventResult result = EventHandled;
		if (exactMatch(ks_moveDown, keySequence)) {
				m_tc.movePosition(Down, move_mode);
		} else if (exactMatch(ks_moveUp, keySequence)) {
				m_tc.movePosition(Up, move_mode);
		} else if (exactMatch(ks_moveStartLine, keySequence)) {
				moveToStartOfLine(move_mode);
		} else if (exactMatch(ks_moveEndLine, keySequence)) {
				moveToEndOfLine(move_mode);
		} else if (exactMatch(ks_moveLeft, keySequence)) {
				moveLeft(1, move_mode);
		} else if (exactMatch(ks_moveRight, keySequence)) {
				moveRight(1, move_mode);
		} else if (exactMatch(ks_moveWordLeft, keySequence)) {
				moveToPreviousWord(move_mode);
		} else if (exactMatch(ks_moveWordRight, keySequence)) {
				moveToNextWord(move_mode);
		} else if (exactMatch(ks_killWord, keySequence)) {
				killWord();
		} else if (exactMatch(ks_backKillWord, keySequence)) {
				backwardKillWord();
		} else if (exactMatch(ks_deleteChar, keySequence)) {
				m_tc.deleteChar();
		} else if (exactMatch(ks_moveDocStart, keySequence)) {
				m_tc.movePosition(StartOfDocument, move_mode);
		} else if (exactMatch(ks_moveDocEnd, keySequence)) {
				m_tc.movePosition(EndOfDocument, move_mode);
		} else if (exactMatch(ks_movePageDown, keySequence)) {
				moveDown((linesOnScreen() - 6) - cursorLineOnScreen(), move_mode);
				scrollToLineInDocument(cursorLineInDocument());
		} else if (exactMatch(ks_movePageUp, keySequence)) {
				moveUp((linesOnScreen() - 6) + cursorLineOnScreen(), move_mode);
				scrollToLineInDocument(cursorLineInDocument() + linesOnScreen() - 6);
		} else if (exactMatch(ks_setMark, keySequence)
				|| exactMatch(ks_setMark2, keySequence)) {
				setMark();
		} else if (exactMatch(ks_killLine, keySequence)) {
				killLine(exactMatch(ks_killLine, lastKeySequence));
		} else if (exactMatch(ks_yank, keySequence)) {
				yank();
		} else if (exactMatch(ks_yankPop, keySequence)) {
				yankPop(EDITOR_WIDGET);
		} else if (exactMatch(ks_cut, keySequence)) {
				cut();
		} else if (exactMatch(ks_copy, keySequence)) {
				copy();
		} else if (exactMatch(ks_moveRecenter, keySequence)) {
				scrollUp(linesOnScreen() / 2 - cursorLineOnScreen());
		} else if (ks_popToMark.matches(keySequence) == QKeySequence::ExactMatch) {
				popToMark(MoveAnchor);
		} else if (ks_exchangeDotAndMark.matches(keySequence) == QKeySequence::ExactMatch) {
				exchangeDotAndMark();
		} else if(exactMatch(ks_removeWhitespace, keySequence)){
			removeWhitespace();
		} else {
			result = EventUnhandled;
		}

		mark = markRing.getMostRecentMark();
		if(mark.active and not (isMovementCommand(keySequence) or exactMatch(ks_setMark, keySequence) or exactMatch(ks_setMark2, keySequence))) {
			markRing.toggleActive();

#if NEW_REGION
			m_tc.clearSelection();
						KEY_DEBUG("Have non-movement key, erase highlight");
			//m_tc.setPosition(m_tc.position(), MoveAnchor);
#else
			onlyMovementSinceMark = false;
			QList<QTextEdit::ExtraSelection> m_selections;
			KEY_DEBUG("Have non-movement key, erase highlight");
			q->selectionChanged(m_selections);
#endif
		}
		// MRJ - new active scheme should eliminate need for this
#if 0
		if(onlyMovementSinceMark) {
			KEY_DEBUG("Only movement, now check valid mark");
			QTextCursor tc = m_tc;
			Mark mark(markRing.getMostRecentMark());
			if (mark.valid) {

#if NEW_REGION
				//int position = m_tc.position();
				//m_tc.setPosition(mark.position);
				//m_tc.setPosition(position, KeepAnchor);
#else
				tc.setPosition(mark.position, KeepAnchor);
				QTextEdit::ExtraSelection sel;
				sel.cursor = tc;
				sel.format = tc.blockCharFormat();
				sel.format.setBackground(QColor(200, 200, 20));
				QList<QTextEdit::ExtraSelection> m_selections;
				m_selections.append(sel);
				KEY_DEBUG("HAVE MARK AND MOVEMENT... SHOULD HIGHLIGHT");
				q->selectionChanged(m_selections);
#endif
			}
		}
#endif

		lastKeySequence = keySequence;
		EDITOR(setTextCursor(m_tc));
		return result;
}

void EmacsKeysHandler::Private::installEventFilter()
{
		EDITOR(installEventFilter(q));
}

void EmacsKeysHandler::Private::setupWidget()
{

		//EDITOR(setCursorWidth(QFontMetrics(ed->font()).width(QChar('x')));
		if (m_textedit) {
				m_textedit->setLineWrapMode(QTextEdit::NoWrap);
		} else if (m_plaintextedit) {
				m_plaintextedit->setLineWrapMode(QPlainTextEdit::NoWrap);
		}
		m_wasReadOnly = EDITOR(isReadOnly());
		//EDITOR(setReadOnly(true));

}

void EmacsKeysHandler::Private::restoreWidget()
{
		EDITOR(setReadOnly(m_wasReadOnly));
		EDITOR(setCursorWidth(m_cursorWidth));
		EDITOR(setOverwriteMode(false));
}

#if 0
void EmacsKeysHandler::Private::moveDown(int n)
{
	QTextCursor::MoveOperation direction = QTextCursor::Down;
	if(n < 0) {
		n = -n;
		direction = QTextCursor::Up;
	}
	for(int i=0; i<n; ++i) {
		m_tc.movePosition(direction, MoveAnchor);
	}
}
#endif


/* if simple is given:
 *  class 0: spaces
 *  class 1: non-spaces
 * else
 *  class 0: spaces
 *  class 1: non-space-or-letter-or-number
 *  class 2: letter-or-number
 */
static int charClass(QChar c, bool simple)
{
		if (simple)
				return c.isSpace() ? 0 : 1;
		if (c.isLetterOrNumber() || c.unicode() == '_')
				return 2;
		return c.isSpace() ? 0 : 1;
}


int EmacsKeysHandler::Private::cursorLineOnScreen() const
{
		if (!editor())
				return 0;
		QRect rect = EDITOR(cursorRect());
		GENERAL_DEBUG("cursorLineOnScreen:" << rect.y() / rect.height());
		return rect.y() / rect.height();
}

int EmacsKeysHandler::Private::linesOnScreen() const
{
		if (!editor())
				return 1;
		QRect rect = EDITOR(cursorRect());
		return EDITOR(height()) / rect.height();
}

int EmacsKeysHandler::Private::cursorLineInDocument() const
{
		return m_tc.block().blockNumber();
}

int EmacsKeysHandler::Private::linesInDocument() const
{
		return m_tc.isNull() ? 0 : m_tc.document()->blockCount();
}

void EmacsKeysHandler::Private::scrollToLineInDocument(int line)
{
		// FIXME: works only for QPlainTextEdit
		QScrollBar *scrollBar = EDITOR(verticalScrollBar());
		//qDebug() << "SCROLL: " << scrollBar->value() << line;
		scrollBar->setValue(line);
}
/* MRJ - do we need this??? */
void EmacsKeysHandler::Private::scrollUp(int count)
{
		scrollToLineInDocument(cursorLineInDocument() - cursorLineOnScreen() - count);
}

QString EmacsKeysHandler::Private::selectedText() const
{
	QTextCursor tc = m_tc;
	tc.setPosition(m_anchor, KeepAnchor);
	return tc.selection().toPlainText();
}

QWidget *EmacsKeysHandler::Private::editor() const
{
		return m_textedit
				? static_cast<QWidget *>(m_textedit)
				: static_cast<QWidget *>(m_plaintextedit);
}

QString EmacsKeysHandler::Private::removeSelectedText()
{
		//qDebug() << "POS: " << position() << " ANCHOR: " << anchor() << m_tc.anchor();
		int pos = m_tc.position();
		if (pos == anchor())
				return QString();
		m_tc.setPosition(anchor(), MoveAnchor);
		m_tc.setPosition(pos, KeepAnchor);
		QString from = m_tc.selection().toPlainText();
		m_tc.removeSelectedText();
		setAnchor();
		return from;
}

///////////////////////////////////////////////////////////////////////
//
// EmacsKeysHandler
//
///////////////////////////////////////////////////////////////////////

EmacsKeysHandler::EmacsKeysHandler(QWidget *widget, QObject *parent)
		: QObject(parent), d(new Private(this, widget))
{}

EmacsKeysHandler::~EmacsKeysHandler()
{
		delete d;
}

/* General idea here is that we get two "events"... two calls of this function...
 * one is the keypress, and
 * the second is the option to override a shortcut... need to return false
 * on both in order to pass the keypress back to qt, need to return true
 * on both to override... I don't know why we'd ever return true on one and
 * false on the other...

*/

bool EmacsKeysHandler::eventFilter(QObject *ob, QEvent *ev)
{
		bool active = theEmacsKeysSetting(ConfigUseEmacsKeys)->value().toBool();

		KEY_DEBUG("STARTING");
		if (active && ev->type() == QEvent::KeyPress && ob == d->editor()) {
				QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
				KEY_DEBUG("KEYPRESS" << kev->key());
				EventResult res = d->handleEvent(kev);
				KEY_DEBUG("ENDING_1, return " << (res==EventHandled ? "true":"false"));
				return res == EventHandled;
		}

		if (active && ev->type() == QEvent::ShortcutOverride && ob == d->editor()) {
				QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
				if (d->wantsOverride(kev)) {
						KEY_DEBUG("OVERRIDING SHORTCUT" << kev->key());
						ev->accept(); // accepting means "don't run the shortcuts"
						KEY_DEBUG("ENDING_2, return true");
						return true;
				}
				KEY_DEBUG("NO SHORTCUT OVERRIDE" << kev->key());
				KEY_DEBUG("ENDING_3, return false");
				return false; // MRJ 3/7 - why was this true?
		}
		bool ret_val = QObject::eventFilter(ob, ev);
		KEY_DEBUG("ENDING_4, return %s" << (ret_val ? "true":"false"));

		return ret_val;
}

void EmacsKeysHandler::installEventFilter()
{
		d->installEventFilter();
}

void EmacsKeysHandler::setupWidget()
{
		d->setupWidget();
}

void EmacsKeysHandler::restoreWidget()
{
		d->restoreWidget();
}

QWidget *EmacsKeysHandler::widget()
{
		return d->editor();
}

} // namespace Internal
} // namespace EmacsKeys
