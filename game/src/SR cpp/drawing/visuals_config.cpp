#include "visuals_config.h"

namespace draw
{
	visuals_config& visuals()
	{
		static visuals_config cfg;
		return cfg;
	}
}
