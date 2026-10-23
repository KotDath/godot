#include "platform/auroraos/orientation.h"

#include <cassert>
int main() {
	using namespace AuroraOrientation;
	using namespace DisplayServerEnums;
	Vector2 size(1200, 2000), p(100, 200), delta(10, 20);
	assert(to_buffer(p, size, 3) == Vector2(200, 1100));
	assert(to_buffer(p, size, 1) == Vector2(1800, 100));
	for (int t = 0; t < 4; t++) {
		Vector2 bsize = t % 2 ? Vector2(size.y, size.x) : size;
		assert(to_buffer(to_buffer(p, size, t), bsize, (4 - t) % 4) == p);
		assert(to_buffer(p + delta, size, t) - to_buffer(p, size, t) == to_buffer(delta, size, t, true));
		assert(select_transform(SCREEN_SENSOR, t, false, 0) == t);
		assert(select_transform(SCREEN_SENSOR_LANDSCAPE, t, false, 3) % 2 == 1);
		assert(select_transform(SCREEN_SENSOR_PORTRAIT, t, false, 0) % 2 == 0);
	}
	assert(select_transform(SCREEN_LANDSCAPE, 0, false, 0) == 3);
	assert(select_transform(SCREEN_LANDSCAPE, 0, true, 0) == 0);
}
