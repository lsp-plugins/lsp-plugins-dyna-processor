/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#ifndef PRIVATE_PLUGINS_DYNA_PROCESSOR_H_
#define PRIVATE_PLUGINS_DYNA_PROCESSOR_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>

#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/dynamics/DynamicProcessor.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/MeterGraph.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>

#include <private/meta/dyna_processor.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Dynamic Processor Plugin Series
         */
        class dyna_processor: public plug::Module
        {
            public:
                enum dyna_mode_t
                {
                    DYNA_MONO,
                    DYNA_STEREO,
                    DYNA_LR,
                    DYNA_MS
                };

            protected:
                enum sc_source_t
                {
                    SCT_FEED_FORWARD,
                    SCT_FEED_BACK,
                    SCT_EXTERNAL
                };

                enum sc_graph_t
                {
                    G_IN,
                    G_SC,
                    G_ENV,
                    G_GAIN,
                    G_OUT,

                    G_TOTAL
                };

                enum sc_meter_t
                {
                    M_IN,
                    M_SC,
                    M_ENV,
                    M_GAIN,
                    M_CURVE,
                    M_OUT,

                    M_TOTAL
                };

                enum sync_t
                {
                    S_CURVE     = 1 << 0,
                    S_MODEL     = 1 << 1,

                    S_ALL       = S_MODEL
                };

                typedef struct channel_t
                {
                    dspu::Bypass            sBypass;            // Bypass
                    dspu::Sidechain         sSC;                // Sidechain module
                    dspu::Equalizer         sSCEq;              // Sidechain equalizer
                    dspu::DynamicProcessor  sProc;              // Processor module
                    dspu::Delay             sLaDelay;           // Lookahead delay
                    dspu::Delay             sInDelay;           // Input compensation delay
                    dspu::Delay             sOutDelay;          // Output compensation delay
                    dspu::Delay             sDryDelay;          // Dry delay
                    dspu::MeterGraph        sGraph[G_TOTAL];    // Meter graph

                    float                  *vIn;                // Input data
                    float                  *vOut;               // Output data
                    float                  *vSc;                // Sidechain data
                    float                  *vEnv;               // Envelope data
                    float                  *vGain;              // Gain reduction data
                    bool                    bScListen;          // Listen sidechain
                    size_t                  nSync;              // Synchronization flags
                    size_t                  nScType;            // Sidechain mode
                    float                   fMakeup;            // Makeup gain
                    float                   fFeedback;          // Feedback
                    float                   fDryGain;           // Dry gain
                    float                   fWetGain;           // Wet gain
                    float                   fDotIn;             // Dot input gain
                    float                   fDotOut;            // Dot output gain

                    plug::IPort            *pIn;                // Input port
                    plug::IPort            *pOut;               // Output port
                    plug::IPort            *pSC;                // Sidechain port

                    plug::IPort            *pGraph[G_TOTAL];    // History graphs
                    plug::IPort            *pMeter[M_TOTAL];    // Meters

                    plug::IPort            *pScType;            // Sidechain location
                    plug::IPort            *pScMode;            // Sidechain mode
                    plug::IPort            *pScLookahead;       // Sidechain lookahead
                    plug::IPort            *pScListen;          // Sidechain listen
                    plug::IPort            *pScSource;          // Sidechain source
                    plug::IPort            *pScReactivity;      // Sidechain reactivity
                    plug::IPort            *pScPreamp;          // Sidechain pre-amplification
                    plug::IPort            *pScHpfMode;         // Sidechain high-pass filter mode
                    plug::IPort            *pScHpfFreq;         // Sidechain high-pass filter frequency
                    plug::IPort            *pScLpfMode;         // Sidechain low-pass filter mode
                    plug::IPort            *pScLpfFreq;         // Sidechain low-pass filter frequency

                    plug::IPort            *pDotOn[meta::dyna_processor_metadata::DOTS];         // Dot enable
                    plug::IPort            *pThreshold[meta::dyna_processor_metadata::DOTS];     // Threshold levels
                    plug::IPort            *pGain[meta::dyna_processor_metadata::DOTS];          // Gain levels
                    plug::IPort            *pKnee[meta::dyna_processor_metadata::DOTS];          // Knees levels
                    plug::IPort            *pAttackOn[meta::dyna_processor_metadata::DOTS];      // Attack enable
                    plug::IPort            *pAttackLvl[meta::dyna_processor_metadata::DOTS];     // Attack levels
                    plug::IPort            *pAttackTime[meta::dyna_processor_metadata::RANGES];  // Attack time
                    plug::IPort            *pReleaseOn[meta::dyna_processor_metadata::DOTS];     // Release enable
                    plug::IPort            *pReleaseLvl[meta::dyna_processor_metadata::DOTS];    // Release levels
                    plug::IPort            *pReleaseTime[meta::dyna_processor_metadata::RANGES]; // Release time
                    plug::IPort            *pLowRatio;          // Low Ratio
                    plug::IPort            *pHighRatio;         // High Ratio
                    plug::IPort            *pMakeup;            // Overall Makeup gain

                    plug::IPort            *pDryGain;           // Dry gain
                    plug::IPort            *pWetGain;           // Wet gain
                    plug::IPort            *pCurve;             // Curve graph
                    plug::IPort            *pModel;             // Curve model
                } channel_t;

            protected:
                size_t                  nMode;          // Working mode
                bool                    bSidechain;     // External side chain
                channel_t              *vChannels;      // Audio channels
                float                  *vCurve;         // Dynamic curve
                float                  *vTime;          // Time points buffer
                bool                    bPause;         // Pause button
                bool                    bClear;         // Clear button
                bool                    bMSListen;      // Mid/Side listen
                float                   fInGain;        // Input gain
                bool                    bUISync;
                core::IDBuffer         *pIDisplay;      // Inline display buffer

                plug::IPort            *pBypass;        // Bypass port
                plug::IPort            *pInGain;        // Input gain
                plug::IPort            *pOutGain;       // Output gain
                plug::IPort            *pPause;         // Pause gain
                plug::IPort            *pClear;         // Cleanup gain
                plug::IPort            *pMSListen;      // Mid/Side listen

                uint8_t                *pData;          // Allocated data

            protected:
                float               process_feedback(channel_t *c, size_t i, size_t channels);
                void                process_non_feedback(channel_t *c, float **in, size_t samples);

            public:
                dyna_processor(const meta::plugin_t *metadata, bool sc, size_t mode);
                virtual ~dyna_processor();

            public:
                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports);
                virtual void        destroy();

                virtual void        update_settings();
                virtual void        update_sample_rate(long sr);
                virtual void        ui_activated();

                virtual void        process(size_t samples);
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height);

                virtual void        dump(dspu::IStateDumper *v) const;
        };
    } // namespace plugins
} // namespace lsp

#endif /* PRIVATE_PLUGINS_DYNA_PROCESSOR_H_ */
