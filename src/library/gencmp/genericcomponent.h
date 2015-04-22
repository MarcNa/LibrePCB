/*
 * EDA4U - Professional EDA for everyone!
 * Copyright (C) 2013 Urban Bruhin
 * http://eda4u.ubruhin.ch/
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

#ifndef LIBRARY_GENERICCOMPONENT_H
#define LIBRARY_GENERICCOMPONENT_H

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/

#include <QtCore>
#include "../libraryelement.h"
#include "gencompsignal.h"
#include "gencompsymbvar.h"
#include "../libraryelementattribute.h"

/*****************************************************************************************
 *  Class GenericComponent
 ****************************************************************************************/

namespace library {

/**
 * @brief The GenericComponent class
 */
class GenericComponent final : public LibraryElement
{
        Q_OBJECT

    public:

        // Constructors / Destructor
        explicit GenericComponent(const QUuid& uuid = QUuid::createUuid(),
                                  const Version& version = Version(),
                                  const QString& author = QString(),
                                  const QString& name_en_US = QString(),
                                  const QString& description_en_US = QString(),
                                  const QString& keywords_en_US = QString()) throw (Exception);
        explicit GenericComponent(const FilePath& xmlFilePath) throw (Exception);
        ~GenericComponent() noexcept;

        // Attributes
        const QList<LibraryElementAttribute*>& getAttributes() const noexcept;
        const LibraryElementAttribute* getAttributeByKey(const QString& key) const noexcept;

        // Default Values
        const QMap<QString, QString>& getDefaultValues() const noexcept;
        QString getDefaultValue(const QStringList& localeOrder = QStringList()) const noexcept;
        void clearDefaultValues() noexcept;
        void addDefaultValue(const QString& locale, const QString& value) noexcept;

        // Prefixes
        const QMap<QString, QString>& getPrefixes() const noexcept;
        QString getPrefix(const QStringList& normOrder = QStringList()) const noexcept;
        const QString& getDefaultPrefixNorm() const noexcept;
        QString getDefaultPrefix() const noexcept;
        void clearPrefixes() noexcept;
        void addPrefix(const QString& norm, const QString& prefix, bool isDefault) noexcept;

        // Signals
        const QList<const GenCompSignal*>& getSignals() const noexcept;
        const GenCompSignal* getSignalByUuid(const QUuid& uuid) const noexcept;
        const GenCompSignal* getSignalOfPin(const QUuid& symbVarUuid, const QUuid& itemUuid,
                                            const QUuid& pinUuid) const noexcept;
        void clearSignals() noexcept;
        void addSignal(const GenCompSignal& signal) noexcept;

        // Symbol Variants
        const QList<const GenCompSymbVar*>& getSymbolVariants() const noexcept;
        const GenCompSymbVar* getSymbolVariantByUuid(const QUuid& uuid) const noexcept;
        const QUuid& getDefaultSymbolVariantUuid() const noexcept;
        const GenCompSymbVar* getDefaultSymbolVariant() const noexcept;
        void clearSymbolVariants() noexcept;
        void addSymbolVariant(const GenCompSymbVar& symbolVariant) noexcept;

        // Symbol Variant Items
        const GenCompSymbVarItem* getSymbVarItem(const QUuid& symbVarUuid,
                                                 const QUuid& itemUuid) const noexcept;

    private:

        // make some methods inaccessible...
        GenericComponent(const GenericComponent& other);
        GenericComponent& operator=(const GenericComponent& rhs);


        // Private Methods
        void parseDomTree(const XmlDomElement& root) throw (Exception);
        XmlDomElement* serializeToXmlDomElement() const throw (Exception);
        bool checkAttributesValidity() const noexcept;


        // Generic Conponent Attributes
        QList<LibraryElementAttribute*> mAttributes; ///< all attributes in a specific order
        QMap<QString, QString> mDefaultValues; ///< key: locale (like "en_US"), value: default value
        QMap<QString, QString> mPrefixes; ///< key: norm, value: prefix
        QString mDefaultPrefixNorm; ///< must be an existing key of #mPrefixes
        QList<const GenCompSignal*> mSignals; ///< empty if the component has no signals
        QList<const GenCompSymbVar*> mSymbolVariants; ///< minimum one entry
        QUuid mDefaultSymbolVariantUuid; ///< must be an existing key of #mSymbolVariants
};

} // namespace library

#endif // LIBRARY_GENERICCOMPONENT_H