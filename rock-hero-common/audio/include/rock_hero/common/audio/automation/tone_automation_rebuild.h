/*!
\file tone_automation_rebuild.h
\brief Rebuilds derived plugin-parameter playback curves from persisted musical automation.
*/

#pragma once

#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Runtime binding of one durable plugin id to its live instance and owning tone.

Rig loads recreate every plugin instance, so persisted automation (keyed by durable minted plugin
ids) reaches the live rig only through a binding resolved at load completion.
*/
struct ToneAutomationBinding
{
    /*! \brief Live plugin instance id; empty when the plugin is not currently loaded. */
    std::string instance_id;

    /*! \brief Tone document whose chain the plugin belongs to. */
    std::string tone_document_ref;
};

/*!
\brief Builds durable-id-to-live-instance bindings from a rig load's reported identities.

Plugins whose tone document carried no minted durable id are skipped: persisted automation can
never reference them. Callers that mint ids for such records (the editor's identity merge) keep
their own binding maintenance instead.

\param tone_chains Per-tone plugin identities reported by a loadLiveRig() completion.
\return Bindings keyed by durable plugin id.
*/
[[nodiscard]] std::unordered_map<std::string, ToneAutomationBinding> makeToneAutomationBindings(
    std::span<const LoadedToneChainIdentities> tone_chains);

/*!
\brief Converts one parameter's musical automation points to edit-timeline curve points.

The musical positions are the persisted truth; the returned seconds are derived through the song
tempo map, so rebuilt curves follow grid and tempo-map edits.

\param tempo_map Song tempo map used to resolve musical positions to seconds.
\param points Musical automation points in ascending position.
\return The equivalent curve points in ascending edit-timeline seconds.
*/
[[nodiscard]] std::vector<AutomationCurvePoint> derivedToneCurvePoints(
    const common::core::TempoMap& tempo_map,
    std::span<const common::core::ToneAutomationPoint> points);

/*!
\brief Derives and writes one parameter's playback curve into the live rig.

Best-effort by design: the musical model is the truth and the next load rebuild reconciles the
derived cache, so a failed write is logged as a warning and never propagated.

\param tone_automation Automation boundary of the live rig holding the plugin.
\param tempo_map Song tempo map used to resolve musical positions to seconds.
\param tone_document_ref Tone document whose chain owns the plugin.
\param instance_id Live plugin instance whose parameter is written.
\param param_id Parameter id within that plugin.
\param points Musical automation points in ascending position.
*/
void writeDerivedToneCurve(
    IToneAutomation& tone_automation, const common::core::TempoMap& tempo_map,
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id, std::span<const common::core::ToneAutomationPoint> points);

/*!
\brief Rebuilds every derived playback curve from an arrangement's persisted musical automation.

Runs after a rig load, because the derived curves are deliberately stripped from persisted plugin
state (the musical model is the single source of truth). Entries whose plugin id has no binding —
or whose bound plugin is not currently loaded — are skipped and reconcile on the next load that
resolves them; individual write failures are logged and skipped by \ref writeDerivedToneCurve's
best-effort contract.

\param tone_automation Automation boundary of the loaded live rig.
\param automation The arrangement's persisted per-parameter automation entries.
\param tempo_map Song tempo map used to resolve musical positions to seconds.
\param bindings Runtime plugin bindings keyed by durable plugin id.
*/
void rebuildToneAutomationCurves(
    IToneAutomation& tone_automation,
    std::span<const common::core::ToneParameterAutomation> automation,
    const common::core::TempoMap& tempo_map,
    const std::unordered_map<std::string, ToneAutomationBinding>& bindings);

} // namespace rock_hero::common::audio
