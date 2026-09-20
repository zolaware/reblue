/**
 * @file    gpu/pipeline/pso_predictor.h
 * @brief   Load-time PSO prediction from BD's technique table plus the
 *          generated state template table in cache/pso_state_templates.h.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 *
 * At model load the predictor knows the HDB-determined part of every PSO the
 * model can bind: the (VS, PS) pairs from the boot-built guest shader table,
 * and the vertex declarations and strides its meshes register. Crossing those
 * with the per-(family, column) state templates yields full pipelines,
 * enqueued on the pso_precache pool under the active load token.
 */
#pragma once

#include <rex/types.h>
#include <string>

namespace bd::gpu {

// visualVa = the guest VisualObject with its base technique assigned.
// loadRequest is true at bdBinaryModelLoadRequest entry, a fresh load that
// drops the model's previous asset links, and false at the late technique store
// hooks (vf04 inits, .mdl parse, wind params), which re-emit techniques against
// the decls of every asset the model requested.
void OnModelTechniqueKnown(u32 visualVa, bool loadRequest);

// After bdLoadModelFindOrCreate returns inside bdBinaryModelLoadRequest
// (0x82140238), linking the shared name-keyed LoadModel asset to the requesting
// VisualObject. An already-loaded asset has cached decls, so the model's
// technique closure crosses with them right away.
void OnLoadModelCreated(u32 loadModelVa, u32 visualVa);

// Brackets bdSceneGraphBuild on the loader thread. loadModelVa is the LoadModel
// asset being built (r30), used only as a registry key, never
// dereferenced. LH_Model preloads build assets with no requester yet, and their
// decls cache and cross when a VisualObject asks for the name.
void OnLoadBegin(u32 loadModelVa);
void OnLoadEnd();

// Per hcgVertexDeclarationRegist return inside the load bracket: a mesh of the
// asset being built uses the declaration in this cache slot at this stream-0
// stride.
void OnDeclRegistered(u32 slotVa, u8 stride);

void ReemitPredictions();

// Pair-level verification: true if a load-time prediction covered this pair.
bool IsPairPredicted(u64 vsHash, u64 psHash);

// "tech=N col=N skin=N vs.vso+ps.pso" if the pair is a technique table pair,
// "" otherwise (post-fx / 2D / hcg-HLSL).
std::string DescribePair(u64 vsHash, u64 psHash);

} // namespace bd::gpu
