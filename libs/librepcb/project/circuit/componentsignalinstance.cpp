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
#include <librepcb/common/exceptions.h>
#include <librepcb/common/scopeguardlist.h>
#include "componentsignalinstance.h"
#include "componentinstance.h"
#include "circuit.h"
#include "netsignal.h"
#include <librepcb/library/cmp/component.h>
#include "../erc/ercmsg.h"
#include "../project.h"
#include "../settings/projectsettings.h"
#include "../schematics/items/si_symbolpin.h"
#include "../boards/items/bi_footprintpad.h"

/*****************************************************************************************
 *  Namespace
 ****************************************************************************************/
namespace librepcb {
namespace project {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

ComponentSignalInstance::ComponentSignalInstance(Circuit& circuit, ComponentInstance& cmpInstance,
                                                 const DomElement& domElement) :
    QObject(&cmpInstance), mCircuit(circuit), mComponentInstance(cmpInstance),
    mComponentSignal(nullptr), mIsAddedToCircuit(false), mNetSignal(nullptr)
{
    // read attributes
    Uuid compSignalUuid = domElement.getAttribute<Uuid>("comp_signal", true);
    mComponentSignal = mComponentInstance.getLibComponent().getSignals().get(compSignalUuid).get(); // can throw
    Uuid netsignalUuid = domElement.getAttribute<Uuid>("netsignal", false, Uuid());
    if (!netsignalUuid.isNull()) {
        mNetSignal = mCircuit.getNetSignalByUuid(netsignalUuid);
        if(!mNetSignal) {
            throw RuntimeError(__FILE__, __LINE__,
                QString(tr("Invalid netsignal UUID: \"%1\"")).arg(netsignalUuid.toStr()));
        }
    }

    init();
}

ComponentSignalInstance::ComponentSignalInstance(Circuit& circuit, ComponentInstance& cmpInstance,
                                                 const library::ComponentSignal& cmpSignal,
                                                 NetSignal* netsignal) :
    QObject(&cmpInstance), mCircuit(circuit), mComponentInstance(cmpInstance),
    mComponentSignal(&cmpSignal), mIsAddedToCircuit(false), mNetSignal(netsignal)
{
    init();
}

void ComponentSignalInstance::init()
{
    // create ERC messages
    mErcMsgUnconnectedRequiredSignal.reset(new ErcMsg(mCircuit.getProject(), *this,
        QString("%1/%2").arg(mComponentInstance.getUuid().toStr()).arg(mComponentSignal->getUuid().toStr()),
        "UnconnectedRequiredSignal", ErcMsg::ErcMsgType_t::CircuitError, QString()));
    mErcMsgForcedNetSignalNameConflict.reset(new ErcMsg(mCircuit.getProject(), *this,
        QString("%1/%2").arg(mComponentInstance.getUuid().toStr()).arg(mComponentSignal->getUuid().toStr()),
        "ForcedNetSignalNameConflict", ErcMsg::ErcMsgType_t::SchematicError, QString()));
    updateErcMessages();

    // register to component attributes changed
    connect(&mComponentInstance, &ComponentInstance::attributesChanged,
            this, &ComponentSignalInstance::updateErcMessages);

    // register to net signal name changed
    if (mNetSignal) {
        connect(mNetSignal, &NetSignal::nameChanged, this,
                &ComponentSignalInstance::netSignalNameChanged);
    }

    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);
}

ComponentSignalInstance::~ComponentSignalInstance() noexcept
{
    Q_ASSERT(!mIsAddedToCircuit);
    Q_ASSERT(!isUsed());
    Q_ASSERT(!arePinsOrPadsUsed());
}

/*****************************************************************************************
 *  Getters
 ****************************************************************************************/

bool ComponentSignalInstance::isNetSignalNameForced() const noexcept
{
    return mComponentSignal->isNetSignalNameForced();
}

QString ComponentSignalInstance::getForcedNetSignalName() const noexcept
{
    QString name = mComponentSignal->getForcedNetName();
    mComponentInstance.replaceVariablesWithAttributes(name, false);
    return name;
}

int ComponentSignalInstance::getRegisteredElementsCount() const noexcept
{
    int count = 0;
    count += mRegisteredSymbolPins.count();
    count += mRegisteredFootprintPads.count();
    return count;
}

bool ComponentSignalInstance::arePinsOrPadsUsed() const noexcept
{
    foreach (const SI_SymbolPin* pin, mRegisteredSymbolPins) {
        if (pin->getNetPoint()) {
            return true;
        }
    }
    foreach (const BI_FootprintPad* pad, mRegisteredFootprintPads) {
        if (pad->isUsed()) {
            return true;
        }
    }
    return false;
}

/*****************************************************************************************
 *  Setters
 ****************************************************************************************/

void ComponentSignalInstance::setNetSignal(NetSignal* netsignal)
{
    if (netsignal == mNetSignal) {
        return;
    }
    if (!mIsAddedToCircuit) {
        throw LogicError(__FILE__, __LINE__);
    }
    if (arePinsOrPadsUsed()) {
        throw LogicError(__FILE__, __LINE__, QString(tr("The net signal of the "
            "component signal \"%1:%2\" cannot be changed because it is still in use!"))
            .arg(mComponentInstance.getName(), mComponentSignal->getName()));
    }
    ScopeGuardList sgl;
    if (mNetSignal) {
        mNetSignal->unregisterComponentSignal(*this); // can throw
        disconnect(mNetSignal, &NetSignal::nameChanged,
                   this, &ComponentSignalInstance::netSignalNameChanged);
        sgl.add([&](){mNetSignal->registerComponentSignal(*this);
                      connect(mNetSignal, &NetSignal::nameChanged,
                      this, &ComponentSignalInstance::netSignalNameChanged);});
    }
    if (netsignal) {
        netsignal->registerComponentSignal(*this); // can throw
        connect(netsignal, &NetSignal::nameChanged,
                this, &ComponentSignalInstance::netSignalNameChanged);
        sgl.add([&](){netsignal->unregisterComponentSignal(*this);
                      disconnect(netsignal, &NetSignal::nameChanged,
                      this, &ComponentSignalInstance::netSignalNameChanged);});
    }
    mNetSignal = netsignal;
    updateErcMessages();
    sgl.dismiss();
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

void ComponentSignalInstance::addToCircuit()
{
    if (mIsAddedToCircuit || isUsed()) {
        throw LogicError(__FILE__, __LINE__);
    }
    if (mNetSignal) {
        mNetSignal->registerComponentSignal(*this); // can throw
    }
    mIsAddedToCircuit = true;
    updateErcMessages();
}

void ComponentSignalInstance::removeFromCircuit()
{
    if (!mIsAddedToCircuit) {
        throw LogicError(__FILE__, __LINE__);
    }
    if (isUsed()) {
        throw RuntimeError(__FILE__, __LINE__,
            QString(tr("The component \"%1\" cannot be removed because it is still in use!"))
            .arg(mComponentInstance.getName()));
    }
    if (mNetSignal) {
        mNetSignal->unregisterComponentSignal(*this); // can throw
    }
    mIsAddedToCircuit = false;
    updateErcMessages();
}

void ComponentSignalInstance::registerSymbolPin(SI_SymbolPin& pin)
{
    if ((!mIsAddedToCircuit) || (pin.getCircuit() != mCircuit)
        || (mRegisteredSymbolPins.contains(&pin)))
    {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredSymbolPins.append(&pin);
}

void ComponentSignalInstance::unregisterSymbolPin(SI_SymbolPin& pin)
{
    if ((!mIsAddedToCircuit) || (!mRegisteredSymbolPins.contains(&pin))) {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredSymbolPins.removeOne(&pin);
}

void ComponentSignalInstance::registerFootprintPad(BI_FootprintPad& pad)
{
    if ((!mIsAddedToCircuit) || (pad.getCircuit() != mCircuit)
        || (mRegisteredFootprintPads.contains(&pad)))
    {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredFootprintPads.append(&pad);
}

void ComponentSignalInstance::unregisterFootprintPad(BI_FootprintPad& pad)
{
    if ((!mIsAddedToCircuit) || (!mRegisteredFootprintPads.contains(&pad))) {
        throw LogicError(__FILE__, __LINE__);
    }
    mRegisteredFootprintPads.removeOne(&pad);
}

void ComponentSignalInstance::serialize(DomElement& root) const
{
    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);

    root.setAttribute("comp_signal", mComponentSignal->getUuid());
    root.setAttribute("netsignal", mNetSignal ? mNetSignal->getUuid() : Uuid());
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

bool ComponentSignalInstance::checkAttributesValidity() const noexcept
{
    if (mComponentSignal == nullptr)  return false;
    return true;
}

/*****************************************************************************************
 *  Private Slots
 ****************************************************************************************/

void ComponentSignalInstance::netSignalNameChanged(const QString& newName) noexcept
{
    Q_UNUSED(newName);
    updateErcMessages();
}

void ComponentSignalInstance::updateErcMessages() noexcept
{
    mErcMsgUnconnectedRequiredSignal->setMsg(
        QString(tr("Unconnected component signal: \"%1\" from \"%2\""))
        .arg(mComponentSignal->getName()).arg(mComponentInstance.getName()));
    mErcMsgForcedNetSignalNameConflict->setMsg(
        QString(tr("Signal name conflict: \"%1\" != \"%2\" (\"%3\" from \"%4\")"))
        .arg((mNetSignal ? mNetSignal->getName() : QString()), getForcedNetSignalName(),
        mComponentSignal->getName(), mComponentInstance.getName()));

    mErcMsgUnconnectedRequiredSignal->setVisible((mIsAddedToCircuit) && (!mNetSignal)
        && (mComponentSignal->isRequired()));
    mErcMsgForcedNetSignalNameConflict->setVisible((mIsAddedToCircuit) && (isNetSignalNameForced())
        && (mNetSignal ? (getForcedNetSignalName() != mNetSignal->getName()) : false));
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace project
} // namespace librepcb
