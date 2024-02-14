///|/ Copyright (c) PR 2016 - 2020 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2016 Alessandro Ranellucci @alranel
///|/
///|/ ModixSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Fill3DHoneycomb_hpp_
#define slic3r_Fill3DHoneycomb_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Fill3DHoneycomb : public Fill
{
public:
    Fill* clone() const override { return new Fill3DHoneycomb(*this); };
    ~Fill3DHoneycomb() override {}

	// require bridge flow since most of this pattern hangs in air
    bool use_bridge_flow() const override { return true; }

protected:
	void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon                 		 expolygon,
	    Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_Fill3DHoneycomb_hpp_
