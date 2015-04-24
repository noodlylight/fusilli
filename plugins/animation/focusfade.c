/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animation-internal.h"

// =====================  Effect: Focus Fade  =========================

// Compute the cross-fade opacity to make the effect look good with every
// window opacity value.
static GLushort
fxFocusFadeComputeOpacity (CompWindow *w, float progress, GLushort opacityInt)
{
    ANIM_WINDOW(w);

    float opacity = opacityInt / (float)OPAQUE;
    float multiplier;

    // for one side of the cross-fade
    if (!aw->walkerOverNewCopy)
        progress = 1 - progress;

    Bool newCopy = aw->walkerOverNewCopy;

    // Reverse behavior if lowering (i.e. not raising)
    Bool lowering = aw->restackInfo && !aw->restackInfo->raised;
    if (lowering)
        newCopy = !newCopy;

    if (w->alpha || (newCopy && opacity >= 0.91f))
	multiplier = decelerateProgress(progress);
    else if (opacity > 0.94f)
	multiplier = decelerateProgressCustom(progress, 0.55, 1.32);
    else if (opacity >= 0.91f && opacity < 0.94f)
	multiplier = decelerateProgressCustom(progress, 0.62, 0.92);
    else if (opacity >= 0.89f && opacity < 0.91f)
	multiplier = decelerateProgress(progress);
    else if (opacity >= 0.84f && opacity < 0.89f)
	multiplier = decelerateProgressCustom(progress, 0.64, 0.80);
    else if (opacity >= 0.79f && opacity < 0.84f)
	multiplier = decelerateProgressCustom(progress, 0.67, 0.77);
    else if (opacity >= 0.54f && opacity < 0.79f)
	multiplier = decelerateProgressCustom(progress, 0.61, 0.69);
    else
	multiplier = progress;

    multiplier = 1 - multiplier;
    float finalOpacity = opacity * multiplier;
    finalOpacity = MIN(finalOpacity, 1);
    finalOpacity = MAX(finalOpacity, 0);

    return (GLushort)(finalOpacity * OPAQUE);
}

void
fxFocusFadeUpdateWindowAttrib(CompWindow * w,
			      WindowPaintAttrib * wAttrib)
{
    float forwardProgress = defaultAnimProgress (w);

    wAttrib->opacity =
	fxFocusFadeComputeOpacity (w, forwardProgress, wAttrib->opacity);
}

