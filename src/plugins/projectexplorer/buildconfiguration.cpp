/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "buildconfiguration.h"

#include "buildsteplist.h"
#include "projectexplorer.h"
#include "kitmanager.h"
#include "target.h"
#include "project.h"
#include "kit.h"

#include <coreplugin/variablemanager.h>
#include <projectexplorer/buildenvironmentwidget.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/algorithm.h>

#include <QDebug>

static const char BUILD_STEP_LIST_COUNT[] = "ProjectExplorer.BuildConfiguration.BuildStepListCount";
static const char BUILD_STEP_LIST_PREFIX[] = "ProjectExplorer.BuildConfiguration.BuildStepList.";
static const char CLEAR_SYSTEM_ENVIRONMENT_KEY[] = "ProjectExplorer.BuildConfiguration.ClearSystemEnvironment";
static const char USER_ENVIRONMENT_CHANGES_KEY[] = "ProjectExplorer.BuildConfiguration.UserEnvironmentChanges";
static const char BUILDDIRECTORY_KEY[] = "ProjectExplorer.BuildConfiguration.BuildDirectory";

namespace ProjectExplorer {
namespace Internal {

class BuildConfigMacroExpander : public Utils::AbstractQtcMacroExpander {
public:
    explicit BuildConfigMacroExpander(const BuildConfiguration *bc) : m_bc(bc) {}
    virtual bool resolveMacro(const QString &name, QString *ret);
private:
    const BuildConfiguration *m_bc;
};

bool BuildConfigMacroExpander::resolveMacro(const QString &name, QString *ret)
{
    if (name == QLatin1String("sourceDir")) {
        *ret = m_bc->target()->project()->projectDirectory().toUserOutput();
        return true;
    }
    if (name == QLatin1String("buildDir")) {
        *ret = m_bc->buildDirectory().toUserOutput();
        return true;
    }
    bool found;
    *ret = Core::VariableManager::value(name.toUtf8(), &found);
    return found;
}
} // namespace Internal

BuildConfiguration::BuildConfiguration(Target *target, const Core::Id id) :
    ProjectConfiguration(target, id),
    m_clearSystemEnvironment(false),
    m_macroExpander(0)
{
    Q_ASSERT(target);
    BuildStepList *bsl = new BuildStepList(this, Core::Id(Constants::BUILDSTEPS_BUILD));
    //: Display name of the build build step list. Used as part of the labels in the project window.
    bsl->setDefaultDisplayName(tr("Build"));
    m_stepLists.append(bsl);
    bsl = new BuildStepList(this, Core::Id(Constants::BUILDSTEPS_CLEAN));
    //: Display name of the clean build step list. Used as part of the labels in the project window.
    bsl->setDefaultDisplayName(tr("Clean"));
    m_stepLists.append(bsl);

    emitEnvironmentChanged();

    connect(target, SIGNAL(kitChanged()),
            this, SLOT(handleKitUpdate()));
    connect(this, SIGNAL(environmentChanged()), this, SLOT(emitBuildDirectoryChanged()));
}

BuildConfiguration::BuildConfiguration(Target *target, BuildConfiguration *source) :
    ProjectConfiguration(target, source),
    m_clearSystemEnvironment(source->m_clearSystemEnvironment),
    m_userEnvironmentChanges(source->m_userEnvironmentChanges),
    m_macroExpander(0),
    m_buildDirectory(source->m_buildDirectory)
{
    Q_ASSERT(target);
    // Do not clone stepLists here, do that in the derived constructor instead
    // otherwise BuildStepFactories might reject to set up a BuildStep for us
    // since we are not yet the derived class!

    emitEnvironmentChanged();

    connect(target, SIGNAL(kitChanged()),
            this, SLOT(handleKitUpdate()));
}

BuildConfiguration::~BuildConfiguration()
{
    delete m_macroExpander;
}

Utils::FileName BuildConfiguration::buildDirectory() const
{
    QString path = QDir::cleanPath(environment().expandVariables(m_buildDirectory.toString()));
    return Utils::FileName::fromString(QDir::cleanPath(QDir(target()->project()->projectDirectory().toString()).absoluteFilePath(path)));
}

Utils::FileName BuildConfiguration::rawBuildDirectory() const
{
    return m_buildDirectory;
}

void BuildConfiguration::setBuildDirectory(const Utils::FileName &dir)
{
    m_buildDirectory = dir;
    emitBuildDirectoryChanged();
}

QList<NamedWidget *> BuildConfiguration::createSubConfigWidgets()
{
    return QList<NamedWidget *>() << new ProjectExplorer::BuildEnvironmentWidget(this);
}

Utils::AbstractMacroExpander *BuildConfiguration::macroExpander()
{
    if (!m_macroExpander)
        m_macroExpander = new Internal::BuildConfigMacroExpander(this);
    return m_macroExpander;
}

QList<Core::Id> BuildConfiguration::knownStepLists() const
{
    return Utils::transform(m_stepLists, [](BuildStepList *list) {
       return list->id();
    });
}

BuildStepList *BuildConfiguration::stepList(Core::Id id) const
{
    foreach (BuildStepList *list, m_stepLists)
        if (id == list->id())
            return list;
    return 0;
}

QVariantMap BuildConfiguration::toMap() const
{
    QVariantMap map(ProjectConfiguration::toMap());
    map.insert(QLatin1String(CLEAR_SYSTEM_ENVIRONMENT_KEY), m_clearSystemEnvironment);
    map.insert(QLatin1String(USER_ENVIRONMENT_CHANGES_KEY), Utils::EnvironmentItem::toStringList(m_userEnvironmentChanges));
    map.insert(QLatin1String(BUILDDIRECTORY_KEY), m_buildDirectory.toString());

    map.insert(QLatin1String(BUILD_STEP_LIST_COUNT), m_stepLists.count());
    for (int i = 0; i < m_stepLists.count(); ++i)
        map.insert(QLatin1String(BUILD_STEP_LIST_PREFIX) + QString::number(i), m_stepLists.at(i)->toMap());

    return map;
}

bool BuildConfiguration::fromMap(const QVariantMap &map)
{
    m_clearSystemEnvironment = map.value(QLatin1String(CLEAR_SYSTEM_ENVIRONMENT_KEY)).toBool();
    m_userEnvironmentChanges = Utils::EnvironmentItem::fromStringList(map.value(QLatin1String(USER_ENVIRONMENT_CHANGES_KEY)).toStringList());
    m_buildDirectory = Utils::FileName::fromString(map.value(QLatin1String(BUILDDIRECTORY_KEY)).toString());

    emitEnvironmentChanged();

    qDeleteAll(m_stepLists);
    m_stepLists.clear();

    int maxI = map.value(QLatin1String(BUILD_STEP_LIST_COUNT), 0).toInt();
    for (int i = 0; i < maxI; ++i) {
        QVariantMap data = map.value(QLatin1String(BUILD_STEP_LIST_PREFIX) + QString::number(i)).toMap();
        if (data.isEmpty()) {
            qWarning() << "No data for build step list" << i << "found!";
            continue;
        }
        BuildStepList *list = new BuildStepList(this, data);
        if (list->isNull()) {
            qWarning() << "Failed to restore build step list" << i;
            delete list;
            return false;
        }
        if (list->id() == Constants::BUILDSTEPS_BUILD)
            list->setDefaultDisplayName(tr("Build"));
        else if (list->id() == Constants::BUILDSTEPS_CLEAN)
            list->setDefaultDisplayName(tr("Clean"));
        m_stepLists.append(list);
    }

    // We currently assume there to be at least a clean and build list!
    QTC_CHECK(knownStepLists().contains(Core::Id(ProjectExplorer::Constants::BUILDSTEPS_BUILD)));
    QTC_CHECK(knownStepLists().contains(Core::Id(ProjectExplorer::Constants::BUILDSTEPS_CLEAN)));

    return ProjectConfiguration::fromMap(map);
}

void BuildConfiguration::emitEnvironmentChanged()
{
    Utils::Environment env = baseEnvironment();
    env.modify(userEnvironmentChanges());
    if (env == m_cachedEnvironment)
        return;
    m_cachedEnvironment = env;
    emit environmentChanged(); // might trigger buildDirectoryChanged signal!
}

void BuildConfiguration::handleKitUpdate()
{
    emitEnvironmentChanged();
}

void BuildConfiguration::emitBuildDirectoryChanged()
{
    if (buildDirectory() != m_lastEmmitedBuildDirectory) {
        m_lastEmmitedBuildDirectory = buildDirectory();
        emit buildDirectoryChanged();
    }
}

Target *BuildConfiguration::target() const
{
    return static_cast<Target *>(parent());
}

Utils::Environment BuildConfiguration::baseEnvironment() const
{
    Utils::Environment result;
    if (useSystemEnvironment())
        result = Utils::Environment::systemEnvironment();
    target()->kit()->addToEnvironment(result);
    return result;
}

QString BuildConfiguration::baseEnvironmentText() const
{
    if (useSystemEnvironment())
        return tr("System Environment");
    else
        return tr("Clean Environment");
}

Utils::Environment BuildConfiguration::environment() const
{
    return m_cachedEnvironment;
}

void BuildConfiguration::setUseSystemEnvironment(bool b)
{
    if (useSystemEnvironment() == b)
        return;
    m_clearSystemEnvironment = !b;
    emitEnvironmentChanged();
}

bool BuildConfiguration::useSystemEnvironment() const
{
    return !m_clearSystemEnvironment;
}

QList<Utils::EnvironmentItem> BuildConfiguration::userEnvironmentChanges() const
{
    return m_userEnvironmentChanges;
}

void BuildConfiguration::setUserEnvironmentChanges(const QList<Utils::EnvironmentItem> &diff)
{
    if (m_userEnvironmentChanges == diff)
        return;
    m_userEnvironmentChanges = diff;
    emitEnvironmentChanged();
}

void BuildConfiguration::cloneSteps(BuildConfiguration *source)
{
    if (source == this)
        return;
    qDeleteAll(m_stepLists);
    m_stepLists.clear();
    foreach (BuildStepList *bsl, source->m_stepLists) {
        BuildStepList *newBsl = new BuildStepList(this, bsl);
        newBsl->cloneSteps(bsl);
        m_stepLists.append(newBsl);
    }
}

bool BuildConfiguration::isEnabled() const
{
    return true;
}

QString BuildConfiguration::disabledReason() const
{
    return QString();
}

///
// IBuildConfigurationFactory
///

IBuildConfigurationFactory::IBuildConfigurationFactory(QObject *parent) :
    QObject(parent)
{ }

IBuildConfigurationFactory::~IBuildConfigurationFactory()
{ }

// restore
IBuildConfigurationFactory *IBuildConfigurationFactory::find(Target *parent, const QVariantMap &map)
{
    return ExtensionSystem::PluginManager::getObject<IBuildConfigurationFactory>(
        [&parent, map](IBuildConfigurationFactory *factory) {
            return factory->canRestore(parent, map);
        });
}

// setup
IBuildConfigurationFactory *IBuildConfigurationFactory::find(Kit *k, const QString &projectPath)
{
    QList<IBuildConfigurationFactory *> factories
            = ExtensionSystem::PluginManager::instance()->getObjects<IBuildConfigurationFactory>();

    return Utils::bestElementOr(factories, 0,
        [&k, &projectPath](IBuildConfigurationFactory *a, IBuildConfigurationFactory *b) {
            return a->priority(k, projectPath) > b->priority(k, projectPath);
        });
}

// create
IBuildConfigurationFactory * IBuildConfigurationFactory::find(Target *parent)
{
    QList<IBuildConfigurationFactory *> factories
            = ExtensionSystem::PluginManager::getObjects<IBuildConfigurationFactory>();

    return Utils::bestElementOr(factories, 0,
                               [&parent](IBuildConfigurationFactory *a, IBuildConfigurationFactory *b) {
                                   return a->priority(parent) > b->priority(parent);
                               });
}

// clone
IBuildConfigurationFactory *IBuildConfigurationFactory::find(Target *parent, BuildConfiguration *bc)
{
    return ExtensionSystem::PluginManager::getObject<IBuildConfigurationFactory>(
        [&parent, &bc](IBuildConfigurationFactory *factory) {
            return factory->canClone(parent, bc);
        });
}
} // namespace ProjectExplorer
