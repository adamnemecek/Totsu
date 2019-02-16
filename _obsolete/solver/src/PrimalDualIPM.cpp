

#include "PrimalDualIPM.h"

#ifndef IPM_DISABLE_LOG
// iostream and std is just for logging
#include <iostream>
#define IPM_LOG_EN(...) {using namespace std; __VA_ARGS__}
#else
#define IPM_LOG_EN(...) /**/
#endif


PrimalDualIPM::PrimalDualIPM()
{
	IPM_LOG_EN({
		m_pOuts = NULL;
	});

	m_margin = IPM_Scalar(1.0);

	m_loop = 256;
	m_bloop = 256;
	m_eps_feas = IPM_Scalar(sqrt(IPM_SCALAR_EPS)); // TODO
	m_eps = IPM_Scalar(sqrt(IPM_SCALAR_EPS)); // TODO

	m_mu = IPM_Scalar(10.0);
	m_alpha = IPM_Scalar(0.1);
	m_beta = IPM_Scalar(0.8);

	m_s_coef = IPM_Scalar(0.99);
}

PrimalDualIPM::~PrimalDualIPM()
{
	// do nothing
}

void PrimalDualIPM::logWarn(const char *str)
{
	IPM_LOG_EN({
		if (!m_pOuts) return;

		*m_pOuts << "----- WARNING: " << str << endl;
	});
}

void PrimalDualIPM::logVector(const char *str, IPM_Vector_IN v)
{
	IPM_LOG_EN({
		if (!m_pOuts) return;

		if (v.cols() == 1)
		{
			*m_pOuts << "----- " << str << "^T :" << endl;
			*m_pOuts << v.transpose() << endl;
		}
		else
		{
			*m_pOuts << "----- " << str << " :" << endl;
			*m_pOuts << v << endl;
		}
	});
}

void PrimalDualIPM::logMatrix(const char *str, IPM_Matrix_IN m)
{
	IPM_LOG_EN({
		if (!m_pOuts) return;

		*m_pOuts << "----- " << str << " :" << endl;
		*m_pOuts << m << endl;
	});
}

IPM_Error PrimalDualIPM::start(const IPM_uint n, const IPM_uint m, const IPM_uint p)
{
	IPM_Error err;
	bool converged = true;

	// parameter check
	if (n == 0) return IPM_ERR_STR;

	/***** matrix *****/
	// constant across loop
	IPM_Matrix A(p, n);
	IPM_Vector b(p);
	// loop variable
	IPM_Vector y(n + m + p), Dy(n + m + p);
	IPM_Matrix kkt(n + m + p, n + m + p);
	// temporal in loop
	IPM_Vector Df_o(n), f_i(m), r_t(n + m + p), y_p(n + m + p);
	IPM_Matrix Df_i(m, n), DDf(n, n);

	/***** sub matrix *****/
	IPM_Vector_IO x = y.segment(0, n);
	IPM_Vector_IO lmd = y.segment(n, m);
	IPM_Vector_IO nu = y.segment(n + m, p);
	IPM_Vector_IO r_dual = r_t.segment(0, n);
	IPM_Vector_IO r_cent = r_t.segment(n, m);
	IPM_Vector_IO r_pri = r_t.segment(n + m, p);
	IPM_Matrix_IO kkt_x_dual = kkt.block(0, 0, n, n);
	IPM_Matrix_IO kkt_lmd_dual = kkt.block(0, n, n, m);
	IPM_Matrix_IO kkt_nu_dual = kkt.block(0, n + m, n, p);
	IPM_Matrix_IO kkt_x_cent = kkt.block(n, 0, m, n);
	IPM_Matrix_IO kkt_lmd_cent = kkt.block(n, n, m, m);
	IPM_Matrix_IO kkt_x_pri = kkt.block(n + m, 0, p, n);
	IPM_Vector_IO Dlmd = Dy.segment(n, m);
	IPM_Vector_IO x_p = y_p.segment(0, n);
	IPM_Vector_IO lmd_p = y_p.segment(n, m);
	IPM_Vector_IO nu_p = y_p.segment(n + m, p);

	/***** KKT matrix decomposition solver *****/
#ifdef IPM_DECOMP_TYPE
	Eigen::IPM_DECOMP_TYPE<IPM_Matrix> decomp_kkt(n + m + p, n + m + p);
#else
	Eigen::JacobiSVD<IPM_Matrix> decomp_kkt(n + m + p, n + m + p, Eigen::ComputeThinU | Eigen::ComputeThinV);
#endif

	// initialize
	if ((err = initialPoint(x)) != NULL) return err;
	lmd.setOnes();
	lmd *= m_margin;
	nu.setZero();
	kkt.setZero();
	if ((err = equality(A, b)) != NULL) return err;

	// initial Df_o, f_i, Df_i
	if ((err = Dobjective(x, Df_o)) != NULL) return err;
	if ((err = inequality(x, f_i)) != NULL) return err;
	if ((err = Dinequality(x, Df_i)) != NULL) return err;

	// inequality feasibility check
	if (f_i.maxCoeff() >= 0) return IPM_ERR_STR;

	// initial residual - dual and primal
	r_dual = Df_o;
	if (m > 0) r_dual += Df_i.transpose() * lmd;
	if (p > 0) r_dual += A.transpose() * nu;
	if (p > 0) r_pri = A * x - b;


	IPM_uint loop = 0;
	for (; loop < m_loop; loop++)
	{
		IPM_Scalar eta, inv_t;

		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << endl << "===== ===== ===== ===== loop : " << loop << endl; });

		/***** calc t *****/

		eta = m_eps;
		if (m > 0) eta = IPM_Scalar(-f_i.dot(lmd));

		// inequality feasibility check
		if (eta < 0) return IPM_ERR_STR;

		inv_t = eta / (m_mu * m);

		/***** update residual - central *****/

		if (m > 0) r_cent = -lmd.cwiseProduct(f_i) - inv_t * IPM_Vector::Ones(m);

		/***** termination criteria *****/

		IPM_Scalar r_dual_norm = r_dual.norm();
		IPM_Scalar r_pri_norm = r_pri.norm();
		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "|| r_dual || : " << r_dual_norm << endl; });
		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "|| r_pri  || : " << r_pri_norm << endl; });
		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "   eta       : " << eta << endl; });
		if ((r_dual_norm <= m_eps_feas) && (r_pri_norm <= m_eps_feas) && (eta <= m_eps))
		{
			IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "termination criteria satisfied" << endl; });
			break;
		}

		/***** calc kkt matrix *****/

		if ((err = DDobjective(x, DDf)) != NULL) return err;
		kkt_x_dual = DDf;
		for (IPM_uint i = 0; i < m; i++)
		{
			if ((err = DDinequality(x, DDf, i)) != NULL) return err;
			kkt_x_dual += lmd(i) * DDf;
		}

		if (m > 0)
		{
			kkt_lmd_dual = Df_i.transpose();

			kkt_x_cent = Df_i;
			for (IPM_uint i = 0; i < m; i++)
			{
				kkt_x_cent.row(i) *= -lmd(i, 0);
			}

			for (IPM_uint i = 0; i < m; i++)
			{
				kkt_lmd_cent(i, i) = -f_i(i, 0);
			}
		}

		if (p > 0)
		{
			kkt_nu_dual = A.transpose();

			kkt_x_pri = A;
		}

		/***** calc search direction *****/

		decomp_kkt.compute(kkt);
		Dy = decomp_kkt.solve(-r_t);
		logVector("y", y);
		//logMatrix("kkt", kkt);
#ifndef IPM_DECOMP_TYPE
		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "kkt rank / full : " << decomp_kkt.rank() << " / " << n + m + p << endl; });
#endif
		logVector("r_t", r_t);
		logVector("Dy", Dy);

		/***** back tracking line search - from here *****/

		IPM_Scalar s_max = 1.0;
		for (IPM_uint i = 0; i < m; i++)
		{
			if (Dlmd(i, 0) < -IPM_SCALAR_MIN) // to avoid zero-division by Dlmd
			{
				s_max = min(s_max, -lmd(i, 0) / Dlmd(i, 0));
			}
		}
		IPM_Scalar s = m_s_coef * s_max;

		y_p = y + s * Dy;

		IPM_uint bloop = 0;
		for (; bloop < m_bloop; bloop++)
		{
			// update f_i
			if ((err = inequality(x_p, f_i)) != NULL) return err;

			if ((f_i.maxCoeff() < 0) && (lmd.minCoeff() > 0)) break;
			s = m_beta * s;
			y_p = y + s * Dy;
		}

		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "s : " << s << endl; });

		if (bloop < m_bloop)
		{
			IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "feasible points found" << endl; });
		}
		else
		{
			IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "infeasible in this direction" << endl; });
		}

		IPM_Scalar org_r_t_norm = r_t.norm();

		for (; bloop < m_bloop; bloop++)
		{
			// update Df_o, f_i, Df_i
			if ((err = Dobjective(x_p, Df_o)) != NULL) return err;
			if ((err = inequality(x_p, f_i)) != NULL) return err;
			if ((err = Dinequality(x_p, Df_i)) != NULL) return err;

			// update residual
			r_dual = Df_o;
			if (m > 0) r_dual += Df_i.transpose() * lmd_p;
			if (p > 0) r_dual += A.transpose() * nu_p;
			if (m > 0) r_cent = -lmd_p.cwiseProduct(f_i) - inv_t * IPM_Vector::Ones(m);
			if (p > 0) r_pri = A * x_p - b;

			if (r_t.norm() <= (1.0 - m_alpha * s) * org_r_t_norm) break;
			s = m_beta * s;
			y_p = y + s * Dy;
		}

		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "s : " << s << endl; });

		if ((bloop < m_bloop) && ((y_p - y).norm() >= IPM_SCALAR_EPS))
		{
			IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "update" << endl; });
			// update y
			y = y_p;
		}
		else
		{
			IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "no more improvement" << endl; });
			converged = false;
			break;
		}

		/***** back tracking line search - to here *****/

	} // end of for

	if (!(loop < m_loop))
	{
		IPM_LOG_EN({ if (m_pOuts) *m_pOuts << "iteration limit" << endl; });
		converged = false;
	}


	IPM_LOG_EN({ if (m_pOuts) *m_pOuts << endl << "===== ===== ===== ===== result" << endl; });
	logVector("x", x);
	logVector("lmd", lmd);
	logVector("nu", nu);

	// finalize
	if ((err = finalPoint(x, lmd, nu, converged)) != NULL) return err;

	return NULL;
}
