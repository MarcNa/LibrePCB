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
#include <QtCore>
#include "componentinstance.h"
#include <librepcb/common/exceptions.h>
#include <librepcb/common/scopeguardlist.h>
#include "circuit.h"
#include "../project.h"
#include "../library/projectlibrary.h"
#include "componentsignalinstance.h"
#include <librepcb/library/cmp/component.h>
#include "../erc/ercmsg.h"
#include "../schematics/items/si_symbol.h"
#include "../boards/items/bi_device.h"

/*****************************************************************************************
 *  Namespace
 ****************************************************************************************/
namespace librepcb {
namespace project {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

ComponentInstance::ComponentInstance(Circuit& circuit, const DomElement& domElement) :
    QObject(&circuit), mCircuit(circuit), mIsAddedToCircuit(false),
    mLibComponent(nullptr), mCompSymbVar(nullptr), mAttributes()
{
    // read general attributes
    mUuid = domElement.getAttribute<Uuid>("uuid", true);
    mName = domElement.getFirstChild("name", true)->getText<QString>(true);
    mValue = domElement.getFirstChild("value", true)->getText<QString>(false);
    Uuid cmpUuid = domElement.getAttribute<Uuid>("component", true);
    mLibComponent = mCircuit.getProject().getLibrary().getComponent(cmpUuid);
    if (!mLibComponent) {
        throw RuntimeError(__FILE__, __LINE__,
            QString(tr("The component with the UUID \"%1\" does not exist in the "
            "project's library!")).arg(cmpUuid.toStr()));
    }
    Uuid symbVarUuid = domElement.getAttribute<Uuid>("symbol_variant", true);
    mCompSymbVar = mLibComponent->getSymbolVariants().get(symbVarUuid).get(); // can throw

    // load all component attributes
    mAttributes.reset(new AttributeList(domElement)); // can throw

    // load all signal instances
    foreach (const DomElement* node, domElement.getChilds("signal_map")) {
        ComponentSignalInstance* signal = new ComponentSignalInstance(mCircuit, *this, *node);
        if (mSignals.contains(signal->getCompSignal().getUuid())) {
            throw RuntimeError(__FILE__, __LINE__,
                QString(tr("The signal with the UUID \"%1\" is defined multiple times."))
                .arg(signal->getCompSignal().getUuid().toStr()));
        }
        mSignals.insert(signal->getCompSignal().getUuid(), signal);
    }
    if (mSignals.count() != mLibComponent->getSignals().count()) {
        qDebug() << mSignals.count() << "!=" << mLibComponent->getSignals().count();
        throw RuntimeError(__FILE__, __LINE__,
            QString(tr("The signal count of the component instance \"%1\" does "
            "not match with the signal count of the component \"%2\"."))
            .arg(mUuid.toStr()).arg(mLibComponent->getUuid().toStr()));
    }

    init();
}

ComponentInstance::ComponentInstance(Circuit& circuit, const library::Component& cmp,
                                     const Uuid& symbVar, const QString& name) :
    QObject(&circuit), mCircuit(circuit), mIsAddedToCircuit(false),
    mUuid(Uuid::createRandom()), mName(name), mLibComponent(&cmp), mCompSymbVar(nullptr),
    mAttributes()
{
    if (mName.isEmpty()) {
        throw RuntimeError(__FILE__, __LINE__,
            tr("The name of the component must not be empty."));
    }
    mValue = cmp.getDefaultValue();
    mCompSymbVar = mLibComponent->getSymbolVariants().get(symbVar).get(); // can throw

    // add attributes
    mAttributes.reset(new AttributeList(cmp.getAttributes()));

    // add signal map
    for (const library::ComponentSignal& signal : cmp.getSignals()) {
        ComponentSignalInstance* signalInstance = new ComponentSignalInstance(
            mCircuit, *this, signal, nullptr);
        mSignals.insert(signalInstance->getCompSignal().getUuid(), signalInstance);
    }

    init();
}

void ComponentInstance::init()
{
    // create ERC messages
    mErcMsgUnplacedRequiredSymbols.reset(new ErcMsg(mCircuit.getProject(), *this, mUuid.toStr(),
        "UnplacedRequiredSymbols", ErcMsg::ErcMsgType_t::SchematicError));
    mErcMsgUnplacedOptionalSymbols.reset(new ErcMsg(mCircuit.getProject(), *this, mUuid.toStr(),
        "UnplacedOptionalSymbols", ErcMsg::ErcMsgType_t::SchematicWarning));
    updateErcMessages();

    // emit the "attributesChanged" signal when the project has emited it
    connect(&mCircuit.getProject(), &Project::attributesChanged, this, &ComponentInstance::attributesChanged);

    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);
}

ComponentInstance::~ComponentInstance() noexcept
{
    Q_ASSERT(!mIsAddedToCircuit);
    Q_ASSERT(!isUsed());

    qDeleteAll(mSignals);       mSignals.clear();
}

/*****************************************************************************************
 *  Getters
 ****************************************************************************************/

QString ComponentInstance::getValue(bool replaceAttributes) const noexcept
{
    QString value = mValue;
    if (replaceAttributes) {
        replaceVariablesWithAttributes(value, false);
    }
    return value;
}

int ComponentInstance::getUnplacedSymbolsCount() const noexcept
{
    return (mCompSymbVar->getSymbolItems().count() - mRegisteredSymbols.count());
}

int ComponentInstance::getUnplacedRequiredSymbolsCount() const noexcept
{
    int count = 0;
    for (const library::ComponentSymbolVariantItem& item : mCompSymbVar->getSymbolItems()) {
        if ((item.isRequired()) && (!mRegisteredSymbols.contains(item.getUuid()))) {
            count++;
        }
    }
    return count;
}

int ComponentInstance::getUnplacedOptionalSymbolsCount() const noexcept
{
    int count = 0;
    for (const library::ComponentSymbolVariantItem& item : mCompSymbVar->getSymbolItems()) {
        if ((!item.isRequired()) && (!mRegisteredSymbols.contains(item.getUuid()))) {
            count++;
        }
    }
    return count;
}

int ComponentInstance::getRegisteredElementsCount() const noexcept
{
    int count = 0;
    count += mRegisteredSymbols.count();
    count += mRegisteredDevices.count();
    return count;
}

bool ComponentInstance::isUsed() const noexcept
{
    if (getRegisteredElementsCount() > 0) {
        return true;
    }
    foreach (const ComponentSignalInstance* signal, mSignals) {
        if (signal->isUsed()) {
            return true;
        }
    }
    return false;
}

/*****************************************************************************************
 *  Setters
 ****************************************************************************************/

void ComponentInstance::setName(const QString& name)
{
    if (name != mName) {
        if(name.isEmpty()) {
            throw RuntimeError(__FILE__, __LINE__,
                tr("The new component name must not be empty!"));
        }
        mName = name;
        updateErcMessages();
        emit attributesChanged();
    }
}

void ComponentInstance::setValue(const QString& value) noexcept
{
    if (value != mValue) {
        mValue = value;
        emit attributesChanged();
    }
}

void ComponentInstance::setAttributes(const AttributeList& attributes) noexcept
{
    if (attributes != *mAttributes) {
        *mAttributes = attributes;
        emit attributesChanged();
    }
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

void ComponentInstance::addToCircuit()
{
    if (mIsAddedToCircuit || isUsed()) {
        throw LogicError(__FILE__, __LINE__);
    }
    ScopeGuardList sgl(mSignals.count());
    foreach (ComponentSignalInstance* signal, mSignals) {
        signal->addToCircuit(); // can throw
        sgl.add([signal](){signal->removeFromCircuit();});
    }
    mIsAddedToCircuit = true;
    updateErcMessages();
    sgl.dismiss();
}

void ComponentInstance::removeFromCircuit()
{
    if (!mIsAddedToCircuit) {
        throw LogicError(__FILE__, __LINE__);
    }
    if (isUsed()) {
        throw RuntimeError(__FILE__, __LINE__,
            QString(tr("The component \"%1\" cannot be removed because it is still in use!"))
            .arg(mName));
    }
    ScopeGuardList sgl(mSignals.count());
    foreach (ComponentSignalInstance* signal, mSignals) {
        signal->removeFromCircuit(); // can throw
        sgl.add([signal](){signal->addToCircuit();});
    }
    mIsAddedToCircuit = false;
    updateErcMessages();
    sgl.dismiss();
}

void ComponentInstance::registerSymbol(SI_Symbol& symbol)
{
    if ((!mIsAddedToCircuit) || (symbol.getCircuit() != mCircuit)) {
        throw LogicError(__FILE__, __LINE__);
    }
    Uuid itemUuid = symbol.getCompSymbVarItem().getUuid();
    if (!mCompSymbVar->getSymbolItems().find(itemUuid)) {
        throw RuntimeError(__FILE__, __LINE__, QString(tr(
            "Invalid symbol item in circuit: \"%1\".")).arg(itemUuid.toStr()));
    }
    if (mRegisteredSymbols.contains(itemUuid)) {
        throw RuntimeError(__FILE__, __LINE__, QString(tr(
            "Symbol item UUID already exists in circuit: \"%1\".")).arg(itemUuid.toStr()));
    }
    if (!mRegisteredSymbols.isEmpty()) {
        if (symbol.getSchematic() != mRegisteredSymbols.values().first()->getSchematic()) {
            // Actually it would be possible to place the symbols of a component on
            // different schematics. But maybe some time this will no longer be possible
            // due to the concept of hierarchical sheets, sub-circuits or something like
            // that. To make the later project upgrade process (as simple as) possible, we
            // introduce this restriction already from now on.
            throw RuntimeError(__FILE__, __LINE__,
                tr("All symbols of a component must be placed in the same schematic."));
        }
    }
    mRegisteredSymbols.insert(itemUuid, &symbol);
    updateErcMessages();
}

void ComponentInstance::unregisterSymbol(SI_Symbol& symbol)
{
    Uuid itemUuid = symbol.getCompSymbVarItem().getUuid();
    if ((!mIsAddedToCircuit) || (!mRegisteredSymbols.contains(itemUuid))
        || (&symbol != mRegisteredSymbols.value(itemUuid)))
    {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredSymbols.remove(itemUuid);
    updateErcMessages();
}

void ComponentInstance::registerDevice(BI_Device& device)
{
    if ((!mIsAddedToCircuit) || (device.getCircuit() != mCircuit)
        || (mRegisteredDevices.contains(&device)) || (mLibComponent->isSchematicOnly()))
    {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredDevices.append(&device);
    updateErcMessages();
}

void ComponentInstance::unregisterDevice(BI_Device& device)
{
    if ((!mIsAddedToCircuit) || (!mRegisteredDevices.contains(&device))) {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredDevices.removeOne(&device);
    updateErcMessages();
}

void ComponentInstance::serialize(DomElement& root) const
{
    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);

    root.setAttribute("uuid", mUuid);
    root.setAttribute("component", mLibComponent->getUuid());
    root.setAttribute("symbol_variant", mCompSymbVar->getUuid());
    root.appendTextChild("name", mName);
    root.appendTextChild("value", mValue);
    mAttributes->serialize(root);
    serializePointerContainer(root, mSignals, "signal_map");
}

/*****************************************************************************************
 *  Helper Methods
 ****************************************************************************************/

bool ComponentInstance::getAttributeValue(const QString& attrNS, const QString& attrKey,
                                        bool passToParents, QString& value) const noexcept
{
    if ((attrNS == QLatin1String("CMP")) || (attrNS.isEmpty())) {
        if (attrKey == QLatin1String("NAME"))
            return value = mName, true;
        else if (attrKey == QLatin1String("VALUE"))
            return value = mValue, true;
        else if (mAttributes->contains(attrKey))
            return value = mAttributes->find(attrKey)->getValueTr(true), true;
    }

    if ((attrNS != QLatin1String("CMP")) && (passToParents))
        return mCircuit.getProject().getAttributeValue(attrNS, attrKey, passToParents, value);
    else
        return false;
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

bool ComponentInstance::checkAttributesValidity() const noexcept
{
    if (mUuid.isNull())             return false;
    if (mName.isEmpty())            return false;
    if (mLibComponent == nullptr)   return false;
    if (mCompSymbVar == nullptr)    return false;
    return true;
}

void ComponentInstance::updateErcMessages() noexcept
{
    int required = getUnplacedRequiredSymbolsCount();
    int optional = getUnplacedOptionalSymbolsCount();
    mErcMsgUnplacedRequiredSymbols->setMsg(
        QString(tr("Unplaced required symbols of component \"%1\": %2"))
        .arg(mName).arg(required));
    mErcMsgUnplacedOptionalSymbols->setMsg(
        QString(tr("Unplaced optional symbols of component \"%1\": %2"))
        .arg(mName).arg(optional));
    mErcMsgUnplacedRequiredSymbols->setVisible((mIsAddedToCircuit) && (required > 0));
    mErcMsgUnplacedOptionalSymbols->setVisible((mIsAddedToCircuit) && (optional > 0));
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace project
} // namespace librepcb
