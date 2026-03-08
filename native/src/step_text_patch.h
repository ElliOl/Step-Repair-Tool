/**
 * Text-level STEP file patcher.
 *
 * Applies name repair and/or HOOPS compat fixes by doing targeted string
 * substitutions on the original file, bypassing the OpenCASCADE writer
 * entirely.  This keeps output files the same size as the originals.
 *
 * Use this path when fixShells is false.  Shell-split requires a full OCCT
 * round-trip and cannot be handled here.
 */
#pragma once
#include <string>

namespace StepFixerNative {

/**
 * Patch a STEP file at the text level without touching OpenCASCADE geometry.
 *
 * fixNames       – replace PRODUCT('0','0',...) with the real part name derived
 *                  from the MANIFOLD_SOLID_BREP name (preferred) or the
 *                  NEXT_ASSEMBLY_USAGE_OCCURRENCE instance name (fallback).
 * fixHoopsCompat – remove all OVER_RIDING_STYLED_ITEM entities and rebuild the
 *                  MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION so it
 *                  only references the base STYLED_ITEM.
 *
 * Returns true on success.
 */
bool PatchStepFileText(
    const std::string& inputPath,
    const std::string& outputPath,
    bool fixNames,
    bool fixHoopsCompat);

/**
 * Same as PatchStepFileText but operates on already-loaded file content.
 * Use this when the caller has the raw bytes in memory (e.g. from the viewer
 * cache) to avoid a redundant disk read.
 */
bool PatchStepContent(
    const std::string& content,
    const std::string& outputPath,
    bool fixNames,
    bool fixHoopsCompat);

} // namespace StepFixerNative
