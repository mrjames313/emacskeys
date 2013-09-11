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

#ifndef EMACSKEYS_HANDLER_H
#define EMACSKEYS_HANDLER_H

#include "emacskeysactions.h"

#include <QObject>
#include <QTextEdit>

namespace EmacsKeys {
namespace Internal {

class EmacsKeysHandler : public QObject
{
    Q_OBJECT

public:
    EmacsKeysHandler(QWidget *widget, QObject *parent = 0);
    ~EmacsKeysHandler();

    QWidget *widget();

public slots:

    void installEventFilter();

    // Convenience
    void setupWidget();
    void restoreWidget();

signals:
		void selectionChanged(const QList<QTextEdit::ExtraSelection> &selection);
    void quitRequested(bool force);
    void quitAllRequested(bool force);

public:
    class Private;

private:
    bool eventFilter(QObject *ob, QEvent *ev);
    friend class Private;
    Private *d;
};

} // namespace Internal
} // namespace EmacsKeys

#endif // EMACSKEYS_H
