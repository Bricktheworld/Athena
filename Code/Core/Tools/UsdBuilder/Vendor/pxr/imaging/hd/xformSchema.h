//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#ifndef PXR_IMAGING_HD_XFORM_SCHEMA_H
#define PXR_IMAGING_HD_XFORM_SCHEMA_H

/// \file

#include "pxr/imaging/hd/api.h"

#include "pxr/imaging/hd/schema.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

// --(BEGIN CUSTOM CODE: Declares)--
// --(END CUSTOM CODE: Declares)--

#define HD_XFORM_SCHEMA_TOKENS \
    (xform) \
    (matrix) \
    (resetXformStack) \

TF_DECLARE_PUBLIC_TOKENS(HdXformSchemaTokens, HD_API,
    HD_XFORM_SCHEMA_TOKENS);

//-----------------------------------------------------------------------------


class HdXformSchema : public HdSchema
{
public:
    /// \name Schema retrieval
    /// @{

    HdXformSchema(HdContainerDataSourceHandle container)
      : HdSchema(container) {}

    /// Retrieves a container data source with the schema's default name token
    /// "xform" from the parent container and constructs a
    /// HdXformSchema instance.
    /// Because the requested container data source may not exist, the result
    /// should be checked with IsDefined() or a bool comparison before use.
    HD_API
    static HdXformSchema GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer);

    /// @}

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

    /// \name Member accessor
    /// @{

    HD_API
    HdMatrixDataSourceHandle GetMatrix() const;

    /// The "resetXformStack" flag tells consumers that this transform doesn't
    /// inherit from the parent prim's transform.
    HD_API
    HdBoolDataSourceHandle GetResetXformStack() const; 

    /// @}

    /// \name Schema location
    /// @{

    /// Returns a token where the container representing this schema is found in
    /// a container by default.
    HD_API
    static const TfToken &GetSchemaToken();

    /// Returns an HdDataSourceLocator (relative to the prim-level data source)
    /// where the container representing this schema is found by default.
    HD_API
    static const HdDataSourceLocator &GetDefaultLocator();

    /// @} 

    /// \name Schema construction
    /// @{

    /// \deprecated Use Builder instead.
    ///
    /// Builds a container data source which includes the provided child data
    /// sources. Parameters with nullptr values are excluded. This is a
    /// low-level interface. For cases in which it's desired to define
    /// the container with a sparse set of child fields, the Builder class
    /// is often more convenient and readable.
    HD_API
    static HdContainerDataSourceHandle
    BuildRetained(
        const HdMatrixDataSourceHandle &matrix,
        const HdBoolDataSourceHandle &resetXformStack
    );

    /// \class HdXformSchema::Builder
    /// 
    /// Utility class for setting sparse sets of child data source fields to be
    /// filled as arguments into BuildRetained. Because all setter methods
    /// return a reference to the instance, this can be used in the "builder
    /// pattern" form.
    class Builder
    {
    public:
        HD_API
        Builder &SetMatrix(
            const HdMatrixDataSourceHandle &matrix);
        HD_API
        Builder &SetResetXformStack(
            const HdBoolDataSourceHandle &resetXformStack);

        /// Returns a container data source containing the members set thus far.
        HD_API
        HdContainerDataSourceHandle Build();

    private:
        HdMatrixDataSourceHandle _matrix;
        HdBoolDataSourceHandle _resetXformStack;

    };

    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif