/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * http://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/
#include "newelementwizardpage_componentsignals.h"
#include "ui_newelementwizardpage_componentsignals.h"
#include <librepcb/library/sym/symbol.h>
#include <librepcb/library/cmp/component.h>
#include <librepcb/workspace/workspace.h>
#include <librepcb/workspace/library/workspacelibrarydb.h>

/*****************************************************************************************
 *  Namespace
 ****************************************************************************************/
namespace librepcb {
namespace library {
namespace editor {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

NewElementWizardPage_ComponentSignals::NewElementWizardPage_ComponentSignals(NewElementWizardContext& context, QWidget *parent) noexcept :
    QWizardPage(parent), mContext(context), mUi(new Ui::NewElementWizardPage_ComponentSignals)
{
    mUi->setupUi(this);
}

NewElementWizardPage_ComponentSignals::~NewElementWizardPage_ComponentSignals() noexcept
{
}

/*****************************************************************************************
 *  Getters
 ****************************************************************************************/

bool NewElementWizardPage_ComponentSignals::validatePage() noexcept
{
    return true;
}

bool NewElementWizardPage_ComponentSignals::isComplete() const noexcept
{
    return true;
}

int NewElementWizardPage_ComponentSignals::nextId() const noexcept
{
    return NewElementWizardContext::ID_ComponentPinSignalMap;
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

QHash<Uuid, QString> NewElementWizardPage_ComponentSignals::getPinNames(const Uuid& symbol,
                                                                        const QString& suffix) const noexcept
{
    QHash<Uuid, QString> names;
    try {
        FilePath fp = mContext.getWorkspace().getLibraryDb().getLatestSymbol(symbol); // can throw
        Symbol sym(fp, true); // can throw
        for (const SymbolPin& pin : sym.getPins()) {
            names.insert(pin.getUuid(), suffix % pin.getName());
        }
    } catch (const Exception& e) {
        // TODO: what could we do here?
    }
    return names;
}

void NewElementWizardPage_ComponentSignals::initializePage() noexcept
{
    QWizardPage::initializePage();

    // automatically create signals if no signals exist
    const ComponentSymbolVariant* variant = mContext.mComponentSymbolVariants.value(0).get();
    if (variant && mContext.mComponentSignals.count() < 1) {
        for (const ComponentSymbolVariantItem& item : variant->getSymbolItems()) {
            QHash<Uuid, QString> names = getPinNames(item.getSymbolUuid(), item.getSuffix());
            for (const ComponentPinSignalMapItem& map : item.getPinSignalMap()) {
                mContext.mComponentSignals.append(std::make_shared<ComponentSignal>(
                    Uuid::createRandom(), names.value(map.getPinUuid())));
            }
        }
    }

    mUi->signalListEditorWidget->setReferences(nullptr, &mContext.mComponentSignals);
}

void NewElementWizardPage_ComponentSignals::cleanupPage() noexcept
{
    QWizardPage::cleanupPage();
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace editor
} // namespace library
} // namespace librepcb
