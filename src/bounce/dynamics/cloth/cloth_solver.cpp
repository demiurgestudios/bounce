/*
* Copyright (c) 2016-2016 Irlan Robson http://www.irlan.net
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

#include <bounce/dynamics/cloth/cloth_solver.h>
#include <bounce/dynamics/cloth/cloth.h>
#include <bounce/dynamics/cloth/particle.h>
#include <bounce/dynamics/cloth/force.h>
#include <bounce/dynamics/cloth/dense_vec3.h>
#include <bounce/dynamics/cloth/diag_mat33.h>
#include <bounce/dynamics/cloth/sparse_sym_mat33.h>
#include <bounce/common/memory/stack_allocator.h>

static B3_FORCE_INLINE void b3Mul(b3DenseVec3& out, const b3SymMat33& A, const b3DenseVec3& v)
{
	B3_ASSERT(A.N == v.n);
	B3_ASSERT(out.n == A.M);

	for (u32 row = 0; row < A.M; ++row)
	{
		out[row].SetZero();
		
		for (u32 column = 0; column < A.N; ++column)
		{
			out[row] += A(row, column) * v[column];
		}
	}
}

static B3_FORCE_INLINE b3DenseVec3 operator*(const b3SymMat33& m, const b3DenseVec3& v)
{
	b3DenseVec3 result(v.n);
	b3Mul(result, m, v);
	return result;
}

// Here, we solve Ax = b using the Modified Preconditioned Conjugate Gradient (MPCG) algorithm.
// described in the paper:
// "Large Steps in Cloth Simulation - David Baraff, Andrew Witkin".

// Some improvements for the original MPCG algorithm are described in the paper:
// "On the modified conjugate gradient method in cloth simulation - Uri M. Ascher, Eddy Boxerman".

u32 b3_clothSolverIterations = 0;

b3ClothSolver::b3ClothSolver(const b3ClothSolverDef& def)
{
	m_allocator = def.stack;

	m_particleCapacity = def.particleCapacity;
	m_particleCount = 0;
	m_particles = (b3Particle**)m_allocator->Allocate(m_particleCapacity * sizeof(b3Particle*));

	m_forceCapacity = def.forceCapacity;
	m_forceCount = 0;
	m_forces = (b3Force**)m_allocator->Allocate(m_forceCapacity * sizeof(b3Force*));;

	m_contactCapacity = def.contactCapacity;
	m_contactCount = 0;
	m_contacts = (b3BodyContact**)m_allocator->Allocate(m_contactCapacity * sizeof(b3BodyContact*));

	m_constraintCapacity = def.particleCapacity;
	m_constraintCount = 0;
	m_constraints = (b3AccelerationConstraint*)m_allocator->Allocate(m_constraintCapacity * sizeof(b3AccelerationConstraint));
}

b3ClothSolver::~b3ClothSolver()
{
	m_allocator->Free(m_constraints);
	m_allocator->Free(m_contacts);
	m_allocator->Free(m_forces);
	m_allocator->Free(m_particles);
}

void b3ClothSolver::Add(b3Particle* p)
{
	p->m_solverId = m_particleCount;
	m_particles[m_particleCount++] = p;
}

void b3ClothSolver::Add(b3BodyContact* c)
{
	m_contacts[m_contactCount++] = c;
}

void b3ClothSolver::Add(b3Force* f)
{
	m_forces[m_forceCount++] = f;
}

void b3ClothSolver::InitializeForces()
{
	for (u32 i = 0; i < m_forceCount; ++i)
	{
		m_forces[i]->Initialize(&m_solverData);
	}
}

void b3ClothSolver::ApplyForces()
{
	for (u32 i = 0; i < m_forceCount; ++i)
	{
		m_forces[i]->Apply(&m_solverData);
	}
}

void b3ClothSolver::InitializeConstraints()
{
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		b3Particle* p = m_particles[i];
		if (p->m_type != e_dynamicParticle)
		{
			b3AccelerationConstraint* ac = m_constraints + m_constraintCount;
			++m_constraintCount;
			ac->i1 = i;
			ac->ndof = 0;
			ac->z.SetZero();
		}
	}

	for (u32 i = 0; i < m_contactCount; ++i)
	{
		b3BodyContact* pc = m_contacts[i];
		b3Particle* p = pc->p1;

		b3AccelerationConstraint* ac = m_constraints + m_constraintCount;
		++m_constraintCount;
		ac->i1 = p->m_solverId;
		ac->ndof = 2;
		ac->p = pc->n;
		ac->z.SetZero();

		if (pc->t1_active && pc->t2_active)
		{
			ac->ndof = 0;
		}
		else
		{
			if (pc->t1_active)
			{
				ac->ndof = 1;
				ac->q = pc->t1;
			}

			if (pc->t2_active)
			{
				ac->ndof = 1;
				ac->q = pc->t2;
			}
		}
	}
}

void b3ClothSolver::Solve(float32 dt, const b3Vec3& gravity)
{
	B3_PROFILE("Integrate");

	b3DenseVec3 sx(m_particleCount);
	b3DenseVec3 sv(m_particleCount);
	b3DenseVec3 sf(m_particleCount);
	b3DenseVec3 sy(m_particleCount);
	b3DenseVec3 sx0(m_particleCount);

	b3SymMat33 dfdx(m_allocator, m_particleCount, m_particleCount);
	dfdx.SetZero();

	b3SymMat33 dfdv(m_allocator, m_particleCount, m_particleCount);
	dfdv.SetZero();

	m_solverData.x = sx.v;
	m_solverData.v = sv.v;
	m_solverData.f = sf.v;
	m_solverData.dfdx = &dfdx;
	m_solverData.dfdv = &dfdv;
	m_solverData.dt = dt;
	m_solverData.invdt = 1.0f / dt;

	for (u32 i = 0; i < m_particleCount; ++i)
	{
		b3Particle* p = m_particles[i];

		sx[i] = p->m_position;
		sv[i] = p->m_velocity;
		sf[i] = p->m_force;

		// Apply weight
		if (p->m_type == e_dynamicParticle)
		{
			sf[i] += p->m_mass * gravity;
		}

		sy[i] = p->m_translation;
		sx0[i] = p->m_x;
	}

	// Apply contact position correction
	for (u32 i = 0; i < m_contactCount; ++i)
	{
		b3BodyContact* c = m_contacts[i];
		b3Particle* p = c->p1;
		sy[p->m_solverId] -= c->s * c->n;
	}

	// Initialize internal forces
	InitializeForces();

	// Apply internal forces
	ApplyForces();

	// Initialize constraints
	InitializeConstraints();

	// Compute S, z
	b3DiagMat33 S(m_particleCount);
	b3DenseVec3 z(m_particleCount);
	Compute_S_z(S, z);

	// Solve Ax = b, where
	// A = M - h * dfdv - h * h * dfdx
	// b = h * (f0 + h * dfdx * v0 + dfdx * y) 

	// A
	b3SparseSymMat33 A(m_allocator, m_particleCount, m_particleCount);

	// b
	b3DenseVec3 b(m_particleCount);

	Compute_A_b(A, b, sf, sx, sv, sy);

	// x
	b3DenseVec3 x(m_particleCount);

	// Solve Ax = b
	u32 iterations = 0;
	Solve(x, iterations, A, b, S, z, sx0);
	b3_clothSolverIterations = iterations;

	// Compute the new state
	// Clamp large translations?
	float32 h = dt;
	sv = sv + x;
	sx = sx + h * sv + sy;

	// Copy state buffers back to the particles
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		m_particles[i]->m_position = sx[i];
		m_particles[i]->m_velocity = sv[i];
	}

	// Cache x to improve convergence
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		m_particles[i]->m_x = x[i];
	}

	// Store the extra contact constraint forces that should have been 
	// supplied to enforce the contact constraints exactly.
	// These forces can be used in contact constraint logic.

	// f = A * x - b
	b3DenseVec3 f = A * x - b;

	for (u32 i = 0; i < m_contactCount; ++i)
	{
		b3BodyContact* c = m_contacts[i];
		b3Particle* p = c->p1;

		b3Vec3 force = f[p->m_solverId];

		// Signed normal force magnitude
		c->Fn = b3Dot(force, c->n);

		// Signed tangent force magnitude
		c->Ft1 = b3Dot(force, c->t1);
		c->Ft2 = b3Dot(force, c->t2);
	}
}

void b3ClothSolver::Compute_A_b(b3SparseSymMat33& A, b3DenseVec3& b, const b3DenseVec3& f, const b3DenseVec3& x, const b3DenseVec3& v, const b3DenseVec3& y) const
{
	float32 h = m_solverData.dt;
	b3SymMat33& dfdx = *m_solverData.dfdx;
	b3SymMat33& dfdv = *m_solverData.dfdv;

	// Compute A
	// A = M - h * dfdv - h * h * dfdx

	// A = 0
	// Set the upper triangle zero
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		for (u32 j = i; j < m_particleCount; ++j)
		{
			A(i, j).SetZero();
		}
	}
	
	// A += M
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		A(i, i) += b3Diagonal(m_particles[i]->m_mass);
	}

	// A += - h * dfdv - h * h * dfdx
	// Set the upper triangle
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		for (u32 j = i; j < m_particleCount; ++j)
		{
			A(i, j) += (-h * dfdv(i, j)) + (-h * h * dfdx(i, j));
		}
	}

	// Compute b

	// b = h * (f0 + h * dfdx * v + dfdx * y)
	b = h * (f + h * (dfdx * v) + dfdx * y);
}

void b3ClothSolver::Compute_S_z(b3DiagMat33& S, b3DenseVec3& z)
{
	S.SetIdentity();
	z.SetZero();

	for (u32 i = 0; i < m_constraintCount; ++i)
	{
		b3AccelerationConstraint* ac = m_constraints + i;
		u32 ip = ac->i1;
		u32 ndof = ac->ndof;
		b3Vec3 p = ac->p;
		b3Vec3 q = ac->q;
		b3Vec3 cz = ac->z;

		z[ip] = cz;

		if (ndof == 2)
		{
			b3Mat33 I; I.SetIdentity();

			S[ip] = I - b3Outer(p, p);
		}

		if (ndof == 1)
		{
			b3Mat33 I; I.SetIdentity();

			S[ip] = I - b3Outer(p, p) - b3Outer(q, q);
		}

		if (ndof == 0)
		{
			S[ip].SetZero();
		}
	}
}

void b3ClothSolver::Solve(b3DenseVec3& x, u32& iterations,
	const b3SparseSymMat33& A, const b3DenseVec3& b, const b3DiagMat33& S, const b3DenseVec3& z, const b3DenseVec3& y) const
{
	B3_PROFILE("Solve Ax = b");

	// P = diag(A)
	b3DiagMat33 inv_P(m_particleCount);
	A.Diagonal(inv_P);

	// Invert
	for (u32 i = 0; i < m_particleCount; ++i)
	{
		b3Mat33& D = inv_P[i];

		// Sylvester Criterion to ensure PD-ness
		B3_ASSERT(b3Det(D.x, D.y, D.z) > 0.0f);

		B3_ASSERT(D.x.x != 0.0f);
		float32 xx = 1.0f / D.x.x;

		B3_ASSERT(D.y.y != 0.0f);
		float32 yy = 1.0f / D.y.y;

		B3_ASSERT(D.z.z != 0.0f);
		float32 zz = 1.0f / D.z.z;

		D = b3Diagonal(xx, yy, zz);
	}

	// I
	b3DiagMat33 I(m_particleCount);
	I.SetIdentity();

	// x = S * y + (I - S) * z 
	x = (S * y) + (I - S) * z;

	// b^ = S * (b - A * (I - S) * z)
	b3DenseVec3 b_hat = S * (b - A * ((I - S) * z));

	// b_delta = dot(b^, P^-1 * b_^)
	float32 b_delta = b3Dot(b_hat, inv_P * b_hat);

	// r = S * (b - A * x)
	b3DenseVec3 r = S * (b - A * x);

	// p = S * (P^-1 * r)
	b3DenseVec3 p = S * (inv_P * r);

	// delta_new = dot(r, p)
	float32 delta_new = b3Dot(r, p);

	// Set the tolerance.
	const float32 tolerance = 1.e-4f;

	// Maximum number of iterations.
	// Stop at this iteration if diverged.
	const u32 max_iterations = 100;

	u32 iteration = 0;

	// Main iteration loop.
	for (;;)
	{
		// Divergence check.
		if (iteration >= max_iterations)
		{
			break;
		}

		B3_ASSERT(b3IsValid(delta_new));

		// Convergence check.
		if (delta_new <= tolerance * tolerance * b_delta)
		{
			break;
		}

		// s = S * (A * p)
		b3DenseVec3 s = S * (A * p);

		// alpha = delta_new / dot(p, s)
		float32 alpha = delta_new / b3Dot(p, s);

		// x = x + alpha * p
		x = x + alpha * p;

		// r = r - alpha * s
		r = r - alpha * s;

		// h = inv_P * r
		b3DenseVec3 h = inv_P * r;

		// delta_old = delta_new
		float32 delta_old = delta_new;

		// delta_new = dot(r, h)
		delta_new = b3Dot(r, h);

		// beta = delta_new / delta_old
		float32 beta = delta_new / delta_old;

		// p = S * (h + beta * p)
		p = S * (h + beta * p);

		++iteration;
	}

	iterations = iteration;
}