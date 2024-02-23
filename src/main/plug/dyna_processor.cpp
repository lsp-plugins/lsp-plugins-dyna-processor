/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-dyna-processor
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-dyna-processor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-dyna-processor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-dyna-processor. If not, see <https://www.gnu.org/licenses/>.
 */

#include <private/plugins/dyna_processor.h>

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/shared/debug.h>
#include <lsp-plug.in/shared/id_colors.h>

#define DYNA_PROC_BUF_SIZE          0x1000

namespace lsp
{
    namespace plugins
    {
        //-------------------------------------------------------------------------
        // Plugin factory
        typedef struct plugin_settings_t
        {
            const meta::plugin_t   *metadata;
            bool                    sc;
            uint8_t                 mode;
        } plugin_settings_t;

        static const meta::plugin_t *plugins[] =
        {
            &meta::dyna_processor_mono,
            &meta::dyna_processor_stereo,
            &meta::dyna_processor_lr,
            &meta::dyna_processor_ms,
            &meta::sc_dyna_processor_mono,
            &meta::sc_dyna_processor_stereo,
            &meta::sc_dyna_processor_lr,
            &meta::sc_dyna_processor_ms
        };

        static const plugin_settings_t plugin_settings[] =
        {
            { &meta::dyna_processor_mono,       false, dyna_processor::DYNA_MONO        },
            { &meta::dyna_processor_stereo,     false, dyna_processor::DYNA_STEREO      },
            { &meta::dyna_processor_lr,         false, dyna_processor::DYNA_LR          },
            { &meta::dyna_processor_ms,         false, dyna_processor::DYNA_MS          },
            { &meta::sc_dyna_processor_mono,    true,  dyna_processor::DYNA_MONO        },
            { &meta::sc_dyna_processor_stereo,  true,  dyna_processor::DYNA_STEREO      },
            { &meta::sc_dyna_processor_lr,      true,  dyna_processor::DYNA_LR          },
            { &meta::sc_dyna_processor_ms,      true,  dyna_processor::DYNA_MS          },

            { NULL, 0, false }
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            for (const plugin_settings_t *s = plugin_settings; s->metadata != NULL; ++s)
                if (s->metadata == meta)
                    return new dyna_processor(s->metadata, s->sc, s->mode);
            return NULL;
        }

        static plug::Factory factory(plugin_factory, plugins, 8);

        //-------------------------------------------------------------------------
        dyna_processor::dyna_processor(const meta::plugin_t *metadata, bool sc, size_t mode): plug::Module(metadata)
        {
            nMode           = mode;
            bSidechain      = sc;
            vChannels       = NULL;
            vCurve          = NULL;
            vTime           = NULL;
            bPause          = false;
            bClear          = false;
            bMSListen       = false;
            bStereoSplit    = false;
            fInGain         = 1.0f;
            bUISync         = true;

            pBypass         = NULL;
            pInGain         = NULL;
            pOutGain        = NULL;
            pPause          = NULL;
            pClear          = NULL;
            pMSListen       = NULL;
            pStereoSplit    = NULL;
            pScSpSource     = NULL;

            pData           = NULL;
            pIDisplay       = NULL;
        }

        dyna_processor::~dyna_processor()
        {
            do_destroy();
        }

        void dyna_processor::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            plug::Module::init(wrapper, ports);
            size_t channels = (nMode == DYNA_MONO) ? 1 : 2;

            // Allocate temporary buffers
            size_t channel_size     = align_size(sizeof(channel_t) * channels, DEFAULT_ALIGN);
            size_t buf_size         = DYNA_PROC_BUF_SIZE * sizeof(float);
            size_t curve_size       = (meta::dyna_processor_metadata::CURVE_MESH_SIZE) * sizeof(float);
            size_t history_size     = (meta::dyna_processor_metadata::TIME_MESH_SIZE) * sizeof(float);
            size_t allocate         = channel_size +
                                      buf_size * channels * 5 +
                                      curve_size +
                                      history_size;

            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, allocate);
            if (ptr == NULL)
                return;

            vChannels               = advance_ptr_bytes<channel_t>(ptr, channel_size);
            vCurve                  = advance_ptr_bytes<float>(ptr, curve_size);
            vTime                   = advance_ptr_bytes<float>(ptr, history_size);

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                // Construct the channel
                channel_t *c = &vChannels[i];
                c->sBypass.construct();
                c->sSC.construct();
                c->sSCEq.construct();
                c->sProc.construct();
                c->sLaDelay.construct();
                c->sInDelay.construct();
                c->sOutDelay.construct();
                c->sDryDelay.construct();
                for (size_t j=0; j<G_TOTAL; ++j)
                    c->sGraph[j].construct();

                // Init the channel
                if (!c->sSC.init(channels, meta::dyna_processor_metadata::REACTIVITY_MAX))
                    return;
                if (!c->sSCEq.init(2, 12))
                    return;
                c->sSCEq.set_mode(dspu::EQM_IIR);
                c->sSC.set_pre_equalizer(&c->sSCEq);

                c->vIn              = advance_ptr_bytes<float>(ptr, buf_size);
                c->vOut             = advance_ptr_bytes<float>(ptr, buf_size);
                c->vSc              = advance_ptr_bytes<float>(ptr, buf_size);
                c->vEnv             = advance_ptr_bytes<float>(ptr, buf_size);
                c->vGain            = advance_ptr_bytes<float>(ptr, buf_size);

                c->bScListen        = false;
                c->nSync            = S_ALL;
                c->nScType          = SCT_FEED_FORWARD;
                c->fMakeup          = 1.0f;
                c->fFeedback        = 0.0f;
                c->fDryGain         = 1.0f;
                c->fWetGain         = 0.0f;
                c->fDotIn           = 0.0f;
                c->fDotOut          = 0.0f;

                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pSC              = NULL;

                for (size_t j=0; j<G_TOTAL; ++j)
                    c->pGraph[j]        = NULL;

                for (size_t j=0; j<M_TOTAL; ++j)
                    c->pMeter[j]        = NULL;

                c->pScType          = NULL;
                c->pScMode          = NULL;
                c->pScLookahead     = NULL;
                c->pScListen        = NULL;
                c->pScSource        = NULL;
                c->pScReactivity    = NULL;
                c->pScPreamp        = NULL;
                c->pScHpfMode       = NULL;
                c->pScHpfFreq       = NULL;
                c->pScLpfMode       = NULL;
                c->pScLpfFreq       = NULL;

                for (size_t j=0; j<meta::dyna_processor_metadata::DOTS; ++j)
                {
                    c->pDotOn[j]        = NULL;
                    c->pThreshold[j]    = NULL;
                    c->pGain[j]         = NULL;
                    c->pKnee[j]         = NULL;
                    c->pAttackOn[j]     = NULL;
                    c->pAttackLvl[j]    = NULL;
                    c->pReleaseOn[j]    = NULL;
                    c->pReleaseLvl[j]   = NULL;
                }
                for (size_t j=0; j<meta::dyna_processor_metadata::RANGES; ++j)
                {
                    c->pAttackTime[j]   = NULL;
                    c->pReleaseTime[j]  = NULL;
                }
                c->pHold            = NULL;

                c->pLowRatio        = NULL;
                c->pHighRatio       = NULL;
                c->pMakeup          = NULL;

                c->pDryWetOn        = NULL;
                c->pDryGain         = NULL;
                c->pWetGain         = NULL;
                c->pDryWet          = NULL;
                c->pCurve           = NULL;
                c->pModel           = NULL;
            }

            lsp_assert(ptr < &pData[allocate]);

            // Bind ports
            size_t port_id              = 0;

            // Input ports
            lsp_trace("Binding input ports");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pIn);

            // Input ports
            lsp_trace("Binding output ports");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pOut);

            // Input ports
            if (bSidechain)
            {
                lsp_trace("Binding sidechain ports");
                for (size_t i=0; i<channels; ++i)
                    BIND_PORT(vChannels[i].pSC);
            }

            // Common ports
            lsp_trace("Binding common ports");
            BIND_PORT(pBypass);
            BIND_PORT(pInGain);
            BIND_PORT(pOutGain);
            BIND_PORT(pPause);
            BIND_PORT(pClear);
            if ((nMode == DYNA_LR) || (nMode == DYNA_MS))
            {
                // Skip processor selector
                SKIP_PORT("Processor selector");
                if (nMode == DYNA_MS)
                    BIND_PORT(pMSListen);
            }
            if (nMode == DYNA_STEREO)
            {
                BIND_PORT(pStereoSplit);
                BIND_PORT(pScSpSource);
            }

            // Sidechain ports
            lsp_trace("Binding sidechain ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                if ((i > 0) && (nMode == DYNA_STEREO))
                {
                    channel_t *sc       = &vChannels[0];
                    c->pScType          = sc->pScType;
                    c->pScSource        = sc->pScSource;
                    c->pScMode          = sc->pScMode;
                    c->pScLookahead     = sc->pScLookahead;
                    c->pScListen        = sc->pScListen;
                    c->pScReactivity    = sc->pScReactivity;
                    c->pScPreamp        = sc->pScPreamp;
                    c->pScHpfMode       = sc->pScHpfMode;
                    c->pScHpfFreq       = sc->pScHpfFreq;
                    c->pScLpfMode       = sc->pScLpfMode;
                    c->pScLpfFreq       = sc->pScLpfFreq;
                }
                else
                {
                    BIND_PORT(c->pScType);
                    BIND_PORT(c->pScMode);
                    BIND_PORT(c->pScLookahead);
                    BIND_PORT(c->pScListen);
                    if (nMode != DYNA_MONO)
                        BIND_PORT(c->pScSource);
                    BIND_PORT(c->pScReactivity);
                    BIND_PORT(c->pScPreamp);
                    BIND_PORT(c->pScHpfMode);
                    BIND_PORT(c->pScHpfFreq);
                    BIND_PORT(c->pScLpfMode);
                    BIND_PORT(c->pScLpfFreq);
                }
            }

            // dyna_processor ports
            lsp_trace("Binding processor ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                if ((i > 0) && (nMode == DYNA_STEREO))
                {
                    channel_t *sc       = &vChannels[0];

                    for (size_t j=0; j<meta::dyna_processor_metadata::DOTS; ++j)
                    {
                        c->pDotOn[j]        = sc->pDotOn[j];
                        c->pThreshold[j]    = sc->pThreshold[j];
                        c->pGain[j]         = sc->pGain[j];
                        c->pKnee[j]         = sc->pKnee[j];
                        c->pAttackOn[j]     = sc->pAttackOn[j];
                        c->pAttackLvl[j]    = sc->pAttackLvl[j];
                        c->pReleaseOn[j]    = sc->pReleaseOn[j];
                        c->pReleaseLvl[j]   = sc->pReleaseLvl[j];
                    }
                    for (size_t j=0; j<meta::dyna_processor_metadata::RANGES; ++j)
                    {
                        c->pAttackTime[j]   = sc->pAttackTime[j];
                        c->pReleaseTime[j]  = sc->pReleaseTime[j];
                    }
                    c->pHold            = sc->pHold;

                    c->pLowRatio        = sc->pLowRatio;
                    c->pHighRatio       = sc->pHighRatio;
                    c->pMakeup          = sc->pMakeup;
                    c->pDryWetOn        = sc->pDryWetOn;
                    c->pDryGain         = sc->pDryGain;
                    c->pWetGain         = sc->pWetGain;
                    c->pDryWet          = sc->pDryWet;
                }
                else
                {
                    BIND_PORT(c->pAttackTime[0]);
                    BIND_PORT(c->pReleaseTime[0]);

                    for (size_t j=0; j<meta::dyna_processor_metadata::DOTS; ++j)
                    {
                        BIND_PORT(c->pDotOn[j]);
                        BIND_PORT(c->pThreshold[j]);
                        BIND_PORT(c->pGain[j]);
                        BIND_PORT(c->pKnee[j]);
                        BIND_PORT(c->pAttackOn[j]);
                        BIND_PORT(c->pAttackLvl[j]);
                        BIND_PORT(c->pAttackTime[j+1]);
                        BIND_PORT(c->pReleaseOn[j]);
                        BIND_PORT(c->pReleaseLvl[j]);
                        BIND_PORT(c->pReleaseTime[j+1]);
                    }
                    BIND_PORT(c->pHold);

                    BIND_PORT(c->pLowRatio);
                    BIND_PORT(c->pHighRatio);
                    BIND_PORT(c->pMakeup);
                    BIND_PORT(c->pDryWetOn);
                    BIND_PORT(c->pDryGain);
                    BIND_PORT(c->pWetGain);
                    BIND_PORT(c->pDryWet);

                    SKIP_PORT("Modelling switch");   // Skip modelling switch

                    BIND_PORT(c->pModel);
                    BIND_PORT(c->pCurve);
                }
            }

            // Bind history
            lsp_trace("Binding history ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                // Skip meters visibility controls
                SKIP_PORT("Sidechain switch");
                SKIP_PORT("Envelope switch");
                SKIP_PORT("Gain reduction switch");
                SKIP_PORT("Input switch");
                SKIP_PORT("Output switch");

                // Bind ports
                BIND_PORT(c->pGraph[G_SC]);
                BIND_PORT(c->pGraph[G_ENV]);
                BIND_PORT(c->pGraph[G_GAIN]);
                BIND_PORT(c->pGraph[G_IN]);
                BIND_PORT(c->pGraph[G_OUT]);
                BIND_PORT(c->pMeter[M_SC]);
                BIND_PORT(c->pMeter[M_CURVE]);
                BIND_PORT(c->pMeter[M_ENV]);
                BIND_PORT(c->pMeter[M_GAIN]);
                BIND_PORT(c->pMeter[M_IN]);
                BIND_PORT(c->pMeter[M_OUT]);
            }

            // Initialize curve (logarithmic) in range of -72 .. +24 db
            float delta = (meta::dyna_processor_metadata::CURVE_DB_MAX - meta::dyna_processor_metadata::CURVE_DB_MIN) / (meta::dyna_processor_metadata::CURVE_MESH_SIZE-1);
            for (size_t i=0; i<meta::dyna_processor_metadata::CURVE_MESH_SIZE; ++i)
                vCurve[i]   = dspu::db_to_gain(meta::dyna_processor_metadata::CURVE_DB_MIN + delta * i);

            // Initialize time points
            delta       = meta::dyna_processor_metadata::TIME_HISTORY_MAX / (meta::dyna_processor_metadata::TIME_MESH_SIZE - 1);
            for (size_t i=0; i<meta::dyna_processor_metadata::TIME_MESH_SIZE; ++i)
                vTime[i]    = meta::dyna_processor_metadata::TIME_HISTORY_MAX - i*delta;
        }

        void dyna_processor::destroy()
        {
            Module::destroy();
            do_destroy();
        }

        void dyna_processor::do_destroy()
        {
            if (vChannels != NULL)
            {
                size_t channels = (nMode == DYNA_MONO) ? 1 : 2;
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c = &vChannels[i];

                    c->sBypass.destroy();
                    c->sSC.destroy();
                    c->sSCEq.destroy();
                    c->sProc.destroy();
                    c->sLaDelay.destroy();
                    c->sInDelay.destroy();
                    c->sOutDelay.destroy();
                    c->sDryDelay.destroy();
                    for (size_t j=0; j<G_TOTAL; ++j)
                        c->sGraph[j].destroy();
                }
                vChannels = NULL;
            }

            if (pData != NULL)
            {
                free_aligned(pData);
                pData = NULL;
            }

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay   = NULL;
            }
        }

        void dyna_processor::update_sample_rate(long sr)
        {
            size_t samples_per_dot  = dspu::seconds_to_samples(sr, meta::dyna_processor_metadata::TIME_HISTORY_MAX / meta::dyna_processor_metadata::TIME_MESH_SIZE);
            size_t channels         = (nMode == DYNA_MONO) ? 1 : 2;
            size_t max_delay        = dspu::millis_to_samples(fSampleRate, meta::dyna_processor_metadata::LOOKAHEAD_MAX);

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];
                c->sBypass.init(sr);
                c->sProc.set_sample_rate(sr);
                c->sSC.set_sample_rate(sr);
                c->sSCEq.set_sample_rate(sr);
                c->sLaDelay.init(max_delay);
                c->sInDelay.init(max_delay);
                c->sOutDelay.init(max_delay);
                c->sDryDelay.init(max_delay);

                for (size_t j=0; j<G_TOTAL; ++j)
                    c->sGraph[j].init(meta::dyna_processor_metadata::TIME_MESH_SIZE, samples_per_dot);
                c->sGraph[G_GAIN].fill(1.0f);
                c->sGraph[G_GAIN].set_method(dspu::MM_MINIMUM);
            }
        }

        dspu::sidechain_source_t dyna_processor::decode_sidechain_source(int source, bool split, size_t channel)
        {
            if (!split)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_MIDDLE;
                    case 1: return dspu::SCS_SIDE;
                    case 2: return dspu::SCS_LEFT;
                    case 3: return dspu::SCS_RIGHT;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            if (channel == 0)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_LEFT;
                    case 1: return dspu::SCS_RIGHT;
                    case 2: return dspu::SCS_MIDDLE;
                    case 3: return dspu::SCS_SIDE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }
            else
            {
                switch (source)
                {
                    case 0: return dspu::SCS_RIGHT;
                    case 1: return dspu::SCS_LEFT;
                    case 2: return dspu::SCS_SIDE;
                    case 3: return dspu::SCS_MIDDLE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            return dspu::SCS_MIDDLE;
        }

        void dyna_processor::update_settings()
        {
            dspu::filter_params_t fp;
            size_t channels = (nMode == DYNA_MONO) ? 1 : 2;
            bool bypass     = pBypass->value() >= 0.5f;

            // Global parameters
            bPause          = pPause->value() >= 0.5f;
            bClear          = pClear->value() >= 0.5f;
            bMSListen       = (pMSListen != NULL) ? pMSListen->value() >= 0.5f : false;
            bStereoSplit    = (pStereoSplit != NULL) ? pStereoSplit->value() >= 0.5f : false;
            fInGain         = pInGain->value();
            float out_gain  = pOutGain->value();
            size_t latency  = 0;

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];
                plug::IPort *sc = (bStereoSplit) ? pScSpSource : c->pScSource;
                size_t sc_src   = (sc != NULL) ? sc->value() : dspu::SCS_MIDDLE;

                // Update bypass settings
                c->sBypass.set_bypass(bypass);

                // Update sidechain settings
                c->nScType      = c->pScType->value();
                c->bScListen    = c->pScListen->value() >= 0.5f;

                c->sSC.set_gain(c->pScPreamp->value());
                c->sSC.set_mode((c->pScMode != NULL) ? c->pScMode->value() : dspu::SCM_RMS);
                c->sSC.set_source(decode_sidechain_source(sc_src, bStereoSplit, i));
                c->sSC.set_reactivity(c->pScReactivity->value());
                c->sSC.set_stereo_mode(((nMode == DYNA_MS) && (c->nScType != SCT_EXTERNAL)) ? dspu::SCSM_MIDSIDE : dspu::SCSM_STEREO);

                // Setup hi-pass filter for sidechain
                size_t hp_slope = c->pScHpfMode->value() * 2;
                fp.nType        = (hp_slope > 0) ? dspu::FLT_BT_BWC_HIPASS : dspu::FLT_NONE;
                fp.fFreq        = c->pScHpfFreq->value();
                fp.fFreq2       = fp.fFreq;
                fp.fGain        = 1.0f;
                fp.nSlope       = hp_slope;
                fp.fQuality     = 0.0f;
                c->sSCEq.set_params(0, &fp);

                // Setup low-pass filter for sidechain
                size_t lp_slope = c->pScLpfMode->value() * 2;
                fp.nType        = (lp_slope > 0) ? dspu::FLT_BT_BWC_LOPASS : dspu::FLT_NONE;
                fp.fFreq        = c->pScLpfFreq->value();
                fp.fFreq2       = fp.fFreq;
                fp.fGain        = 1.0f;
                fp.nSlope       = lp_slope;
                fp.fQuality     = 0.0f;
                c->sSCEq.set_params(1, &fp);

                // Update delay
                size_t delay    = dspu::millis_to_samples(fSampleRate, (c->pScLookahead != NULL) ? c->pScLookahead->value() : 0);
                c->sLaDelay.set_delay(delay);
                latency         = lsp_max(latency, delay);

                // Update processor settings
                c->sProc.set_attack_time(0, c->pAttackTime[0]->value());
                c->sProc.set_release_time(0, c->pReleaseTime[0]->value());

                for (size_t j=0; j<meta::dyna_processor_metadata::DOTS; ++j)
                {
                    c->sProc.set_attack_level(j, (c->pAttackOn[j]->value() >= 0.5f) ? c->pAttackLvl[j]->value() : -1.0f);
                    c->sProc.set_attack_time(j+1, c->pAttackTime[j+1]->value());

                    c->sProc.set_release_level(j, (c->pReleaseOn[j]->value() >= 0.5f) ? c->pReleaseLvl[j]->value() : -1.0f);
                    c->sProc.set_release_time(j+1, c->pReleaseTime[j+1]->value());

                    if ((c->pDotOn[j] != NULL) && (c->pDotOn[j]->value() >= 0.5f))
                        c->sProc.set_dot(j, c->pThreshold[j]->value(), c->pGain[j]->value(), c->pKnee[j]->value());
                    else
                        c->sProc.set_dot(j, -1.0f, -1.0f, -1.0f);
                }

                float makeup = c->pMakeup->value();
                float out_ratio = c->pHighRatio->value();
                if ((c->nScType == SCT_FEED_BACK) && (out_ratio >= 1.0f)) // Prevent from infinite feedback
                    out_ratio = 1.0f;
                c->sProc.set_hold(c->pHold->value());
                c->sProc.set_in_ratio(c->pLowRatio->value());
                c->sProc.set_out_ratio(out_ratio);

                if (c->fMakeup != makeup)
                {
                    c->fMakeup          = makeup;
                    c->nSync           |= S_CURVE;
                }

                // Update gains
                if (c->pDryWetOn->value() >= 0.5f)
                {
                    const float drywet  = c->pDryWet->value() * 0.01f;
                    c->fDryGain         = (1.0f - drywet) * out_gain;
                    c->fWetGain         = drywet * out_gain;
                }
                else
                {
                    c->fDryGain         = c->pDryGain->value() * out_gain;
                    c->fWetGain         = c->pWetGain->value() * out_gain;
                }

                // Check modification flag
                if (c->sProc.modified())
                {
                    c->sProc.update_settings();
                    c->nSync           |= S_CURVE | S_MODEL;
                }
            }

            // Tune compensation delays
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];
                c->sInDelay.set_delay(latency);
                c->sOutDelay.set_delay(latency - c->sLaDelay.get_delay());
                c->sDryDelay.set_delay(latency);
            }

            // Report latency
            set_latency(latency);
        }

        void dyna_processor::ui_activated()
        {
            size_t channels     = (nMode == DYNA_MONO) ? 1 : 2;
            for (size_t i=0; i<channels; ++i)
                vChannels[i].nSync     = S_CURVE | S_MODEL;
            bUISync             = true;
        }

        float dyna_processor::process_feedback(channel_t *c, size_t i, size_t channels)
        {
            // Read input samples
            float in[2];
            if (channels > 1)
            {
                in[0] = vChannels[0].fFeedback;
                in[1] = vChannels[1].fFeedback;
            }
            else
            {
                in[0] = c->fFeedback;
                in[1] = 0.0f;
            }

            // Process sidechain
            float scin      = c->sSC.process(in);

            // Perform compression routine
            c->vGain[i]     = c->sProc.process(&c->vEnv[i], scin);
            c->vOut[i]      = c->vGain[i] * c->vIn[i];

            return scin;
        }

        void dyna_processor::process_non_feedback(channel_t *c, float **in, size_t samples)
        {
            c->sSC.process(c->vSc, const_cast<const float **>(in), samples);
            c->sProc.process(c->vGain, c->vEnv, c->vSc, samples);
            dsp::mul3(c->vOut, c->vGain, c->vIn, samples); // Adjust gain for input
        }

        void dyna_processor::process(size_t samples)
        {
            size_t channels = (nMode == DYNA_MONO) ? 1 : 2;
            size_t feedback = 0;

            float *in_buf[2];   // Input buffer
            float *out_buf[2];  // Output buffer
            float *sc_buf[2];   // Sidechain source
            float *in[2];       // Buffet to pass to sidechain

            // Prepare audio channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Initialize pointers
                in_buf[i]           = c->pIn->buffer<float>();
                out_buf[i]          = c->pOut->buffer<float>();
                sc_buf[i]           = (c->pSC != NULL) ? c->pSC->buffer<float>() : in_buf[i];

                // Analyze channel mode
                if (c->nScType == SCT_FEED_BACK)
                    feedback |= (1 << i);
            }

            // Perform compression
            size_t left = samples;
            while (left > 0)
            {
                // Detemine number of samples to process
                size_t to_process = (left > DYNA_PROC_BUF_SIZE) ? DYNA_PROC_BUF_SIZE : left;

                // Prepare audio channels
                if (nMode == DYNA_MONO)
                    dsp::mul_k3(vChannels[0].vIn, in_buf[0], fInGain, to_process);
                else if (nMode == DYNA_MS)
                {
                    dsp::lr_to_ms(vChannels[0].vIn, vChannels[1].vIn, in_buf[0], in_buf[1], to_process);
                    dsp::mul_k2(vChannels[0].vIn, fInGain, to_process);
                    dsp::mul_k2(vChannels[1].vIn, fInGain, to_process);
                }
                else
                {
                    dsp::mul_k3(vChannels[0].vIn, in_buf[0], fInGain, to_process);
                    dsp::mul_k3(vChannels[1].vIn, in_buf[1], fInGain, to_process);
                }

                // Process meters
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    // Update input graph
                    c->sGraph[G_IN].process(c->vIn, to_process);
                    c->pMeter[M_IN]->set_value(dsp::abs_max(c->vIn, to_process));
                }

                // Do compression
                switch (feedback)
                {
                    case 0:
                    {
                        if (channels > 1) // Process second channel in stereo pair
                        {
                            // First channel
                            in[0]   = (vChannels[0].nScType == SCT_EXTERNAL) ? sc_buf[0] : vChannels[0].vIn;
                            in[1]   = (vChannels[0].nScType == SCT_EXTERNAL) ? sc_buf[1] : vChannels[1].vIn;
                            process_non_feedback(&vChannels[0], in, to_process);
                            vChannels[0].fFeedback      = vChannels[0].vOut[to_process-1];

                            // Second channel
                            in[0]   = (vChannels[1].nScType == SCT_EXTERNAL) ? sc_buf[0] : vChannels[0].vIn;
                            in[1]   = (vChannels[1].nScType == SCT_EXTERNAL) ? sc_buf[1] : vChannels[1].vIn;
                            process_non_feedback(&vChannels[1], in, to_process);
                            vChannels[1].fFeedback      = vChannels[1].vOut[to_process-1];
                        }
                        else
                        {
                            // Only one channel
                            in[0]   = (vChannels[0].nScType == SCT_EXTERNAL) ? sc_buf[0] : vChannels[0].vIn;
                            in[1]   = NULL;
                            process_non_feedback(&vChannels[0], in, to_process);
                            vChannels[0].fFeedback      = vChannels[0].vOut[to_process-1];
                        }

                        break;
                    }

                    case 1:
                    {
                        // 0=FB [1=FF/EXT]
                        if (channels > 1)
                        {
                            // Second channel
                            in[0]   = (vChannels[1].nScType == SCT_EXTERNAL) ? sc_buf[0] : vChannels[0].vIn;
                            in[1]   = (vChannels[1].nScType == SCT_EXTERNAL) ? sc_buf[1] : vChannels[1].vIn;
                            process_non_feedback(&vChannels[1], in, to_process);

                            // Process feedback channel
                            for (size_t i=0; i<to_process; ++i)
                            {
                                vChannels[0].vSc[i]     = process_feedback(&vChannels[0], i, channels);
                                vChannels[0].fFeedback  = vChannels[0].vOut[i];
                                vChannels[1].fFeedback  = vChannels[1].vOut[i];
                            }
                        }
                        else
                        {
                            // Process feedback channel
                            for (size_t i=0; i<to_process; ++i)
                            {
                                vChannels[0].vSc[i]     = process_feedback(&vChannels[0], i, channels);
                                vChannels[0].fFeedback  = vChannels[0].vOut[i];
                            }
                        }

                        break;
                    }

                    case 2:
                    {
                        // 0=FF/EXT 1=FB
                        // First channel
                        in[0]   = (vChannels[0].nScType == SCT_EXTERNAL) ? sc_buf[0] : vChannels[0].vIn;
                        in[1]   = (vChannels[0].nScType == SCT_EXTERNAL) ? sc_buf[1] : vChannels[1].vIn;
                        process_non_feedback(&vChannels[0], in, to_process);

                        // Process feedback channel
                        for (size_t i=0; i<to_process; ++i)
                        {
                            vChannels[1].vSc[i]     = process_feedback(&vChannels[1], i, channels);
                            vChannels[1].fFeedback  = vChannels[1].vOut[i];
                            vChannels[0].fFeedback  = vChannels[0].vOut[i];
                        }

                        break;
                    }

                    case 3:
                    {
                        // 0=FB, 1=FB
                        for (size_t i=0; i<to_process; ++i)
                        {
                            vChannels[0].vSc[i]     = process_feedback(&vChannels[0], i, channels);
                            vChannels[1].vSc[i]     = process_feedback(&vChannels[1], i, channels);
                            vChannels[0].fFeedback  = vChannels[0].vOut[i];
                            vChannels[1].fFeedback  = vChannels[1].vOut[i];
                        }
                        break;
                    }
                    default:
                        break;
                }

                // Apply gain to each channel and process meters
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    // Add delay to original signal and apply gain
                    c->sLaDelay.process(c->vOut, c->vIn, c->vGain, to_process);
                    c->sInDelay.process(c->vIn, c->vIn, to_process);
                    c->sOutDelay.process(c->vOut, c->vOut, to_process);

                    // Process graph outputs
                    c->sGraph[G_SC].process(c->vSc, to_process);                        // Sidechain signal
                    c->pMeter[M_SC]->set_value(dsp::abs_max(c->vSc, to_process));

                    c->sGraph[G_GAIN].process(c->vGain, to_process);                    // Gain reduction signal
                    c->pMeter[M_GAIN]->set_value(dsp::abs_max(c->vGain, to_process));

                    c->sGraph[G_ENV].process(c->vEnv, to_process);                      // Envelope signal
                    c->pMeter[M_ENV]->set_value(dsp::abs_max(c->vEnv, to_process));
                }

                // Form output signal
                if (nMode == DYNA_MS)
                {
                    channel_t *cm       = &vChannels[0];
                    channel_t *cs       = &vChannels[1];

                    dsp::mix2(cm->vOut, cm->vIn, cm->fMakeup * cm->fWetGain, cm->fDryGain, to_process);
                    dsp::mix2(cs->vOut, cs->vIn, cs->fMakeup * cs->fWetGain, cs->fDryGain, to_process);

                    cm->sGraph[G_OUT].process(cm->vOut, to_process);
                    cm->pMeter[M_OUT]->set_value(dsp::abs_max(cm->vOut, to_process));
                    cs->sGraph[G_OUT].process(cs->vOut, to_process);
                    cs->pMeter[M_OUT]->set_value(dsp::abs_max(cs->vOut, to_process));

                    if (!bMSListen)
                        dsp::ms_to_lr(cm->vOut, cs->vOut, cm->vOut, cs->vOut, to_process);
                    if (cm->bScListen)
                        dsp::copy(cm->vOut, cm->vSc, to_process);
                    if (cs->bScListen)
                        dsp::copy(cs->vOut, cs->vSc, to_process);
                }
                else
                {
                    for (size_t i=0; i<channels; ++i)
                    {
                        // Mix dry/wet signal or copy sidechain signal
                        channel_t *c        = &vChannels[i];
                        if (c->bScListen)
                            dsp::copy(c->vOut, c->vSc, to_process);
                        else
                            dsp::mix2(c->vOut, c->vIn, c->fMakeup * c->fWetGain, c->fDryGain, to_process);

                        c->sGraph[G_OUT].process(c->vOut, to_process);                      // Output signal
                        c->pMeter[M_OUT]->set_value(dsp::abs_max(c->vOut, to_process));
                    }
                }

                // Final metering
                for (size_t i=0; i<channels; ++i)
                {
                    // Apply bypass
                    channel_t *c        = &vChannels[i];
                    c->sDryDelay.process(c->vIn, in_buf[i], to_process);
                    c->sBypass.process(out_buf[i], c->vIn, c->vOut, to_process);

                    in_buf[i]          += to_process;
                    out_buf[i]         += to_process;
                    sc_buf[i]          += to_process;
                }

                left       -= to_process;
            }

            if ((!bPause) || (bClear) || (bUISync))
            {
                // Process mesh requests
                for (size_t i=0; i<channels; ++i)
                {
                    // Get channel
                    channel_t *c        = &vChannels[i];

                    for (size_t j=0; j<G_TOTAL; ++j)
                    {
                        // Check that port is bound
                        if (c->pGraph[j] == NULL)
                            continue;

                        // Clear data if requested
                        if (bClear)
                            dsp::fill_zero(c->sGraph[j].data(), meta::dyna_processor_metadata::TIME_MESH_SIZE);

                        // Get mesh
                        plug::mesh_t *mesh    = c->pGraph[j]->buffer<plug::mesh_t>();
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            // Fill mesh with new values
                            dsp::copy(mesh->pvData[0], vTime, meta::dyna_processor_metadata::TIME_MESH_SIZE);
                            dsp::copy(mesh->pvData[1], c->sGraph[j].data(), meta::dyna_processor_metadata::TIME_MESH_SIZE);
                            mesh->data(2, meta::dyna_processor_metadata::TIME_MESH_SIZE);
                        }
                    } // for j
                }

                bUISync = false;
            }

            // Output curves for each channel
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c       = &vChannels[i];

                // Output curve model
                if (c->pModel != NULL)
                {
                    plug::mesh_t *mesh            = c->pModel->buffer<plug::mesh_t>();
                    if ((c->nSync & S_CURVE) && (mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vCurve, meta::dyna_processor_metadata::CURVE_MESH_SIZE);
                        c->sProc.model(mesh->pvData[1], vCurve, meta::dyna_processor_metadata::CURVE_MESH_SIZE);

                        // Mark mesh containing data
                        mesh->data(2, meta::dyna_processor_metadata::CURVE_MESH_SIZE);
                        c->nSync &= ~S_MODEL;
                    }
                }

                // Output curve
                if (c->pCurve != NULL)
                {
                    plug::mesh_t *mesh            = c->pCurve->buffer<plug::mesh_t>();
                    if ((c->nSync & S_CURVE) && (mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vCurve, meta::dyna_processor_metadata::CURVE_MESH_SIZE);
                        c->sProc.curve(mesh->pvData[1], vCurve, meta::dyna_processor_metadata::CURVE_MESH_SIZE);
                        if (c->fMakeup != 1.0f)
                            dsp::mul_k2(mesh->pvData[1], c->fMakeup, meta::dyna_processor_metadata::CURVE_MESH_SIZE);

                        // Mark mesh containing data
                        mesh->data(2, meta::dyna_processor_metadata::CURVE_MESH_SIZE);
                        c->nSync &= ~S_CURVE;
                    }
                }

                // Update meter
                if ((c->pMeter[M_ENV] != NULL) && (c->pMeter[M_CURVE] != NULL))
                {
                    c->fDotIn   = c->pMeter[M_ENV]->value();
                    c->fDotOut  = c->sProc.curve(c->fDotIn) * c->fMakeup;
                    c->pMeter[M_CURVE]->set_value(c->fDotOut);
                }
            }

            // Request for redraw
            if (pWrapper != NULL)
                pWrapper->query_display_draw();
        }

        bool dyna_processor::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > width)
                height  = width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypassing = vChannels[0].sBypass.bypassing();
            cv->set_color_rgb((bypassing) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            float zx    = 1.0f/GAIN_AMP_M_72_DB;
            float zy    = 1.0f/GAIN_AMP_M_72_DB;
            float dx    = width/(logf(GAIN_AMP_P_24_DB)-logf(GAIN_AMP_M_72_DB));
            float dy    = height/(logf(GAIN_AMP_M_72_DB)-logf(GAIN_AMP_P_24_DB));

            // Draw horizontal and vertical lines
            cv->set_line_width(1.0);
            cv->set_color_rgb((bypassing) ? CV_SILVER: CV_YELLOW, 0.5f);
            for (float i=GAIN_AMP_M_72_DB; i<GAIN_AMP_P_24_DB; i *= GAIN_AMP_P_24_DB)
            {
                float ax = dx*(logf(i*zx));
                float ay = height + dy*(logf(i*zy));
                cv->line(ax, 0, ax, height);
                cv->line(0, ay, width, ay);
            }

            // Draw 1:1 line
            cv->set_line_width(2.0);
            cv->set_color_rgb(CV_GRAY);
            {
                float ax1 = dx*(logf(GAIN_AMP_M_72_DB*zx));
                float ax2 = dx*(logf(GAIN_AMP_P_24_DB*zx));
                float ay1 = height + dy*(logf(GAIN_AMP_M_72_DB*zy));
                float ay2 = height + dy*(logf(GAIN_AMP_P_24_DB*zy));
                cv->line(ax1, ay1, ax2, ay2);
            }

            // Draw axis
            cv->set_color_rgb((bypassing) ? CV_SILVER : CV_WHITE);
            {
                float ax = dx*(logf(GAIN_AMP_0_DB*zx));
                float ay = height + dy*(logf(GAIN_AMP_0_DB*zy));
                cv->line(ax, 0, ax, height);
                cv->line(0, ay, width, ay);
            }

            // Reuse display
            pIDisplay           = core::IDBuffer::reuse(pIDisplay, 4, width);
            core::IDBuffer *b   = pIDisplay;
            if (b == NULL)
                return false;

            static const uint32_t c_colors[] =
            {
                CV_MIDDLE_CHANNEL,
                CV_LEFT_CHANNEL, CV_RIGHT_CHANNEL,
                CV_MIDDLE_CHANNEL, CV_SIDE_CHANNEL
            };
            size_t curves       = ((nMode == DYNA_MONO) || (nMode == DYNA_STEREO)) ? 1 : 2;
            const uint32_t *vc  = (curves == 1) ? &c_colors[0] :
                                  (nMode == DYNA_MS) ? &c_colors[3] :
                                  &c_colors[1];

            bool aa = cv->set_anti_aliasing(true);
            lsp_finally { cv->set_anti_aliasing(aa); };
            cv->set_line_width(2);

            for (size_t i=0; i<curves; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t j=0; j<width; ++j)
                {
                    size_t k        = (j*meta::dyna_processor_metadata::CURVE_MESH_SIZE)/width;
                    b->v[0][j]      = vCurve[k];
                }
                c->sProc.curve(b->v[1], b->v[0], width);
                if (c->fMakeup != 1.0f)
                    dsp::mul_k2(b->v[1], c->fMakeup, width);

                dsp::fill(b->v[2], 0.0f, width);
                dsp::fill(b->v[3], height, width);
                dsp::axis_apply_log1(b->v[2], b->v[0], zx, dx, width);
                dsp::axis_apply_log1(b->v[3], b->v[1], zy, dy, width);

                // Draw mesh
                uint32_t color = (bypassing || !(active())) ? CV_SILVER : vc[i];
                cv->set_color_rgb(color);
                cv->draw_lines(b->v[2], b->v[3], width);
            }

            // Draw dot
            if (active())
            {
                size_t channels     = ((nMode == DYNA_MONO) || ((nMode == DYNA_STEREO) && (!bStereoSplit))) ? 1 : 2;
                const uint32_t *vd  = (channels == 1) ? &c_colors[0] :
                                      (nMode == DYNA_MS) ? &c_colors[3] :
                                      &c_colors[1];

                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    uint32_t color = (bypassing) ? CV_SILVER : vd[i];
                    Color c1(color), c2(color);
                    c2.alpha(0.9);

                    float ax = dx*(logf(c->fDotIn*zx));
                    float ay = height + dy*(logf(c->fDotOut*zy));

                    cv->radial_gradient(ax, ay, c1, c2, 12);
                    cv->set_color_rgb(0);
                    cv->circle(ax, ay, 4);
                    cv->set_color_rgb(color);
                    cv->circle(ax, ay, 3);
                }
            }

            return true;
        }

        void dyna_processor::dump(dspu::IStateDumper *v) const
        {
            plug::Module::dump(v);

            size_t channels = (nMode == DYNA_MONO) ? 1 : 2;

            v->write("nMode", nMode);
            v->write("nChannels", channels);
            v->write("bSidechain", bSidechain);

            v->begin_array("vChannels", vChannels, channels);
            for (size_t i=0; i<channels; ++i)
            {
                const channel_t *c = &vChannels[i];

                v->begin_object(c, sizeof(channel_t));
                {
                    v->write_object("sBypass", &c->sBypass);
                    v->write_object("sSC", &c->sSC);
                    v->write_object("sSCEq", &c->sSCEq);
                    v->write_object("sProc", &c->sProc);
                    v->write_object("sLaDelay", &c->sLaDelay);
                    v->write_object("sInDelay", &c->sInDelay);
                    v->write_object("sOutDelay", &c->sOutDelay);
                    v->write_object("sDryDelay", &c->sDryDelay);
                    v->begin_array("sGraph", c->sGraph, G_TOTAL);
                    for (size_t j=0; j<G_TOTAL; ++j)
                        v->write_object(&c->sGraph[j]);
                    v->end_array();

                    v->write("vIn", c->vIn);
                    v->write("vOut", c->vOut);
                    v->write("vSc", c->vSc);
                    v->write("vEnv", c->vEnv);
                    v->write("vGain", c->vGain);
                    v->write("bScListen", c->bScListen);
                    v->write("nSync", c->nSync);
                    v->write("nScType", c->nScType);
                    v->write("fMakeup", c->fMakeup);
                    v->write("fFeedback", c->fFeedback);
                    v->write("fDryGain", c->fDryGain);
                    v->write("fWetGain", c->fWetGain);
                    v->write("fDotIn", c->fDotIn);
                    v->write("fDotOut", c->fDotOut);

                    v->write("pIn", c->pIn);
                    v->write("pOut", c->pOut);
                    v->write("pSC", c->pSC);
                    v->begin_array("pGraph", c->pGraph, G_TOTAL);
                    for (size_t j=0; j<G_TOTAL; ++j)
                        v->write(c->pGraph[j]);
                    v->end_array();
                    v->begin_array("pMeter", c->pGraph, M_TOTAL);
                    for (size_t j=0; j<M_TOTAL; ++j)
                        v->write(c->pMeter[j]);
                    v->end_array();

                    v->write("pScType", c->pScType);
                    v->write("pScMode", c->pScMode);
                    v->write("pScLookahead", c->pScLookahead);
                    v->write("pScListen", c->pScListen);
                    v->write("pScSource", c->pScSource);
                    v->write("pScReactivity", c->pScReactivity);
                    v->write("pScPreamp", c->pScPreamp);
                    v->write("pScHpfMode", c->pScHpfMode);
                    v->write("pScHpfFreq", c->pScHpfFreq);
                    v->write("pScLpfMode", c->pScLpfMode);
                    v->write("pScLpfFreq", c->pScLpfFreq);

                    v->writev("pDotOn", c->pDotOn, meta::dyna_processor_metadata::DOTS);
                    v->writev("pThreshold", c->pThreshold, meta::dyna_processor_metadata::DOTS);
                    v->writev("pGain", c->pGain, meta::dyna_processor_metadata::DOTS);
                    v->writev("pKnee", c->pKnee, meta::dyna_processor_metadata::DOTS);
                    v->writev("pAttackOn", c->pAttackOn, meta::dyna_processor_metadata::DOTS);
                    v->writev("pAttackLvl", c->pAttackLvl, meta::dyna_processor_metadata::DOTS);
                    v->writev("pAttackTime", c->pAttackTime, meta::dyna_processor_metadata::RANGES);
                    v->writev("pReleaseOn", c->pReleaseOn, meta::dyna_processor_metadata::DOTS);
                    v->writev("pReleaseLvl", c->pReleaseLvl, meta::dyna_processor_metadata::DOTS);
                    v->writev("pReleaseTime", c->pReleaseTime, meta::dyna_processor_metadata::RANGES);
                    v->write("pHold", c->pHold);
                    v->write("pLowRatio", c->pLowRatio);
                    v->write("pHighRatio", c->pHighRatio);
                    v->write("pMakeup", c->pMakeup);

                    v->write("pDryWetOn", c->pDryWetOn);
                    v->write("pDryGain", c->pDryGain);
                    v->write("pWetGain", c->pWetGain);
                    v->write("pDryWet", c->pDryWet);
                    v->write("pCurve", c->pCurve);
                    v->write("pModel", c->pModel);
                }
                v->end_object();
            }
            v->end_array();

            v->write("vCurve", vCurve);
            v->write("vTime", vTime);
            v->write("bPause", bPause);
            v->write("bClear", bClear);
            v->write("bMSListen", bMSListen);
            v->write("bStereoSplit", bStereoSplit);
            v->write("fInGain", fInGain);
            v->write("bUISync", bUISync);
            v->write("pIDisplay", pIDisplay);

            v->write("pBypass", pBypass);
            v->write("pInGain", pInGain);
            v->write("pOutGain", pOutGain);
            v->write("pPause", pPause);
            v->write("pClear", pClear);
            v->write("pMSListen", pMSListen);
            v->write("pStereoSplit", pStereoSplit);
            v->write("pScSpSource", pScSpSource);

            v->write("pData", pData);
        }

    } /* namespace plugins */
} /* namespace lsp */


