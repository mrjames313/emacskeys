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

#include "emacskeysplugin.h"

#include "emacskeyshandler.h"
#include "ui_emacskeysoptions.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/id.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/session.h>

#include <texteditor/basetextdocumentlayout.h>
#include <texteditor/basetexteditor.h>
#include <texteditor/basetextmark.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/typingsettings.h>
//#include <texteditor/tabsettings.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/indenter.h>

#include <find/textfindconstants.h>

#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <texteditor/indenter.h>


#include <QDebug>
#include <QtPlugin>
#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QHash>

#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QMainWindow>
#include <QMenu>

using namespace EmacsKeys::Internal;
using namespace TextEditor;
using namespace Core;
using namespace ProjectExplorer;


namespace EmacsKeys {
namespace Constants {

const char INSTALL_HANDLER[]        = "TextEditor.EmacsKeysHandler";

} // namespace Constants
} // namespace EmacsKeys


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysOptionPage
//
///////////////////////////////////////////////////////////////////////

namespace EmacsKeys {
namespace Internal {

class EmacsKeysOptionPage : public Core::IOptionsPage
{
    Q_OBJECT

public:
    EmacsKeysOptionPage() {}

    // IOptionsPage
    QString id() const { return QLatin1String("General"); }
    QString trName() const { return tr("General"); }
    QString category() const { return QLatin1String("EmacsKeys"); }
    QString trCategory() const { return tr("EmacsKeys"); }
    QString displayName() const { return tr("General"); }
    QString displayCategory() const { return tr("EmacsKeys"); }
    QIcon categoryIcon() const { return QIcon(); }

    QWidget *createPage(QWidget *parent);
    void apply() { m_group.apply(ICore::instance()->settings()); }
    void finish() { m_group.finish(); }

private:
    friend class DebuggerPlugin;
    Ui::EmacsKeysOptionPage m_ui;

    Utils::SavedActionSet m_group;
};

QWidget *EmacsKeysOptionPage::createPage(QWidget *parent)
{
    QWidget *w = new QWidget(parent);
    m_ui.setupUi(w);

    m_group.clear();
    m_group.insert(theEmacsKeysSetting(ConfigUseEmacsKeys), 
        m_ui.checkBoxUseEmacsKeys);
    return w;
}


} // namespace Internal
} // namespace EmacsKeys


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysPluginPrivate
//
///////////////////////////////////////////////////////////////////////

namespace EmacsKeys {
namespace Internal {


class EmacsKeysPluginPrivate : public QObject
{
    Q_OBJECT

public:
    EmacsKeysPluginPrivate(EmacsKeysPlugin *);
    ~EmacsKeysPluginPrivate();
    friend class EmacsKeysPlugin;

    bool initialize();
    void shutdown();

private slots:
    void editorOpened(Core::IEditor *);
    void editorAboutToClose(Core::IEditor *);

    void setUseEmacsKeys(const QVariant &value);
    void showSettingsDialog();

    void changeSelection(const QList<QTextEdit::ExtraSelection> &selections);

private:
    EmacsKeysPlugin *q;
    EmacsKeysOptionPage *m_emacsKeysOptionsPage;
    QHash<Core::IEditor *, EmacsKeysHandler *> m_editorToHandler;

    void triggerAction(const Core::Id &id);

};

} // namespace Internal
} // namespace EmacsKeys

EmacsKeysPluginPrivate::EmacsKeysPluginPrivate(EmacsKeysPlugin *plugin)
{       
    q = plugin;
    m_emacsKeysOptionsPage = 0;
}

EmacsKeysPluginPrivate::~EmacsKeysPluginPrivate()
{
}

void EmacsKeysPluginPrivate::shutdown()
{
    q->removeObject(m_emacsKeysOptionsPage);
    delete m_emacsKeysOptionsPage;
    m_emacsKeysOptionsPage = 0;
    theEmacsKeysSettings()->writeSettings(Core::ICore::instance()->settings());
    delete theEmacsKeysSettings();
}

bool EmacsKeysPluginPrivate::initialize()
{
    Core::ActionManager *actionManager = Core::ICore::instance()->actionManager();
    QTC_ASSERT(actionManager, return false);



    m_emacsKeysOptionsPage = new EmacsKeysOptionPage;
    q->addObject(m_emacsKeysOptionsPage);
    theEmacsKeysSettings()->readSettings(Core::ICore::instance()->settings());
    
    Context globalcontext(Core::Constants::C_GLOBAL);
    Core::Command *cmd = 0;
    cmd = actionManager->registerAction(theEmacsKeysSetting(ConfigUseEmacsKeys),
        Constants::INSTALL_HANDLER, globalcontext);

    ActionContainer *advancedMenu =
        actionManager->actionContainer(Core::Constants::M_EDIT_ADVANCED);
    advancedMenu->addAction(cmd, Core::Constants::G_EDIT_EDITOR);

#if 0
/* MRJ - testing */
		QList<Command *> command_list = actionManager->commands();
		foreach(Command* command, command_list) {
			qDebug() << "  Command id: " << command->id().toString();
		}
		/* MRJ - end testing */
#endif

    // EditorManager
    QObject *editorManager = Core::ICore::instance()->editorManager();
    connect(editorManager, SIGNAL(editorAboutToClose(Core::IEditor*)),
        this, SLOT(editorAboutToClose(Core::IEditor*)));
    connect(editorManager, SIGNAL(editorOpened(Core::IEditor*)),
        this, SLOT(editorOpened(Core::IEditor*)));

    connect(theEmacsKeysSetting(SettingsDialog), SIGNAL(triggered()),
        this, SLOT(showSettingsDialog()));
    connect(theEmacsKeysSetting(ConfigUseEmacsKeys), SIGNAL(valueChanged(QVariant)),
        this, SLOT(setUseEmacsKeys(QVariant)));

    return true;
}

void EmacsKeysPluginPrivate::showSettingsDialog()
{
    Core::ICore::instance()->showOptionsDialog("EmacsKeys", "General");
}

void EmacsKeysPluginPrivate::triggerAction(const Id &id)
{
    Core::ActionManager *am = ICore::actionManager();
    QTC_ASSERT(am, return);
    Core::Command *cmd = am->command(id);
    QTC_ASSERT(cmd, qDebug() << "UNKNOWN CODE: " << id.name(); return);
    QAction *action = cmd->action();
    QTC_ASSERT(action, return);
    action->trigger();
}

void EmacsKeysPluginPrivate::editorOpened(Core::IEditor *editor)
{
    if (!editor)
        return;

    QWidget *widget = editor->widget();
    if (!widget)
        return;

    // we can only handle QTextEdit and QPlainTextEdit
    if (!qobject_cast<QTextEdit *>(widget) && !qobject_cast<QPlainTextEdit *>(widget))
        return;
    
    EmacsKeysHandler *handler = new EmacsKeysHandler(widget, widget);
    m_editorToHandler[editor] = handler;

    connect(handler, SIGNAL(selectionChanged(QList<QTextEdit::ExtraSelection>)),
        this, SLOT(changeSelection(QList<QTextEdit::ExtraSelection>)));

    handler->installEventFilter();
    
}

void EmacsKeysPluginPrivate::editorAboutToClose(Core::IEditor *editor)
{
    //qDebug() << "CLOSING: " << editor << editor->widget();
    m_editorToHandler.remove(editor);
}

void EmacsKeysPluginPrivate::setUseEmacsKeys(const QVariant &value)
{
    qDebug() << "SET USE EMACSKEYS" << value;
    bool on = value.toBool();
    if (on) {
        foreach (Core::IEditor *editor, m_editorToHandler.keys())
            m_editorToHandler[editor]->setupWidget();
    } else {
        foreach (Core::IEditor *editor, m_editorToHandler.keys())
            m_editorToHandler[editor]->restoreWidget();
    }
}

void EmacsKeysPluginPrivate::changeSelection
    (const QList<QTextEdit::ExtraSelection> &selection)
{
    if (EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender()))
        if (BaseTextEditorWidget *bt = qobject_cast<BaseTextEditorWidget *>(handler->widget()))
            bt->setExtraSelections(BaseTextEditorWidget::FakeVimSelection, selection);
}


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysPlugin
//
///////////////////////////////////////////////////////////////////////

EmacsKeysPlugin::EmacsKeysPlugin()
    : d(new EmacsKeysPluginPrivate(this))
{}

EmacsKeysPlugin::~EmacsKeysPlugin()
{
    delete d;
}

bool EmacsKeysPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments);
    Q_UNUSED(errorMessage);
    return d->initialize();
}

void EmacsKeysPlugin::shutdown()
{
    d->shutdown();
}

void EmacsKeysPlugin::extensionsInitialized()
{
}

#include "emacskeysplugin.moc"

Q_EXPORT_PLUGIN(EmacsKeysPlugin)
