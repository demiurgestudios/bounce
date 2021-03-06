/*
* Copyright (c) 2016-2019 Irlan Robson https://irlanrobson.github.io
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#ifndef COLLIDE_H
#define COLLIDE_H

class Collide : public Test
{
public:
	Collide()
	{

	}

	void Step()
	{
		b3ConvexCache cache;
		cache.simplexCache.count = 0;
		cache.featureCache.m_featurePair.state = b3SATCacheType::e_empty;

		b3Manifold manifold;
		manifold.Initialize();

		b3CollideShapeAndShape(manifold, m_xfA, m_shapeA, m_xfB, m_shapeB, &cache);
		
		for (u32 i = 0; i < manifold.pointCount; ++i)
		{
			b3WorldManifold wm;
			wm.Initialize(&manifold, m_shapeA->m_radius, m_xfA, m_shapeB->m_radius, m_xfB);

			b3Vec3 pw = wm.points[i].point;
			b3Vec2 ps = g_camera->ConvertWorldToScreen(pw);
			
			g_draw->DrawPoint(pw, 4.0f, b3Color_green);
			g_draw->DrawSegment(pw, pw + wm.points[i].normal, b3Color_white);
		}

		m_world.DrawShape(m_xfA, m_shapeA, b3Color_black);
		m_world.DrawShape(m_xfB, m_shapeB, b3Color_black);

		g_draw->Flush();

		m_world.DrawSolidShape(m_xfA, m_shapeA, b3Color(1.0f, 1.0f, 1.0f, 0.25f));
		m_world.DrawSolidShape(m_xfB, m_shapeB, b3Color(1.0f, 1.0f, 1.0f, 0.25f));

		g_draw->DrawString(b3Color_white, "Left/Right/Up/Down Arrow - Translate shape");
		g_draw->DrawString(b3Color_white, "X/Y/Z - Rotate shape");
		
		g_draw->Flush();
	}

	virtual void KeyDown(int key)
	{
		if (key == GLFW_KEY_LEFT)
		{
			m_xfB.position.x -= 0.05f;
		}

		if (key == GLFW_KEY_RIGHT)
		{
			m_xfB.position.x += 0.05f;
		}
		
		if (key == GLFW_KEY_UP)
		{
			m_xfB.position.y += 0.05f;
		}
		
		if (key == GLFW_KEY_DOWN)
		{
			m_xfB.position.y -= 0.05f;
		}

		if (key == GLFW_KEY_X)
		{
			b3Quat qx(b3Vec3(1.0f, 0.0f, 0.0f), 0.05f * B3_PI);
			b3Mat33 xfx = b3QuatMat33(qx);

			m_xfB.rotation = m_xfB.rotation * xfx;
		}

		if (key == GLFW_KEY_Y)
		{
			b3Quat qy(b3Vec3(0.0f, 1.0f, 0.0f), 0.05f * B3_PI);
			b3Mat33 xfy = b3QuatMat33(qy);

			m_xfB.rotation = m_xfB.rotation * xfy;
		}

		if (key == GLFW_KEY_Z)
		{
			b3Quat qy(b3Vec3(0.0f, 0.0f, 1.0f), 0.05f * B3_PI);
			b3Mat33 xfz = b3QuatMat33(qy);

			m_xfB.rotation = m_xfB.rotation * xfz;
		}
	}

	b3Shape* m_shapeA;
	b3Transform m_xfA;
	
	b3Shape* m_shapeB;
	b3Transform m_xfB;
	
	b3SimplexCache m_cache;
};

#endif
