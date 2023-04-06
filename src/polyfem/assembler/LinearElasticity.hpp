#pragma once

#include <polyfem/assembler/Assembler.hpp>
#include <polyfem/assembler/MatParams.hpp>
#include <polyfem/utils/AutodiffTypes.hpp>
#include <polyfem/utils/ElasticityUtils.hpp>

// local assembler for linear elasticity
namespace polyfem::assembler
{
	class LinearElasticity : public LinearAssembler, NLAssembler, ElasticityAssembler
	{
	public:
		using LinearAssembler::assemble;
		using NLAssembler::assemble_energy;
		using NLAssembler::assemble_gradient;
		using NLAssembler::assemble_hessian;

		/// computes local stiffness matrix is R^{dim²} for bases i,j
		// vals stores the evaluation for that element
		// da contains both the quadrature weight and the change of metric in the integral
		Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 9, 1>
		assemble(const LinearAssemblerData &data) const override;

		// compute elastic energy
		double compute_energy(const NonLinearAssemblerData &data) const override;
		// neccessary for mixing linear model with non-linear collision response
		Eigen::MatrixXd assemble_hessian(const NonLinearAssemblerData &data) const override;
		// compute gradient of elastic energy, as assembler
		Eigen::VectorXd assemble_gradient(const NonLinearAssemblerData &data) const override;

		// kernel of the pde, used in kernel problem
		Eigen::Matrix<AutodiffScalarGrad, Eigen::Dynamic, 1, 0, 3, 1> kernel(const int dim, const AutodiffGradPt &r, const AutodiffScalarGrad &) const override;

		// uses autodiff to compute the rhs for a fabbricated solution
		// uses autogenerated code to compute div(sigma)
		// pt is the evaluation of the solution at a point
		VectorNd compute_rhs(const AutodiffHessianPt &pt) const override;

		// inialize material parameter
		void add_multimaterial(const int index, const json &params) override;

		// class that stores and compute lame parameters per point
		const LameParameters &lame_params() const { return params_; }
		void set_params(const LameParameters &params) { params_ = params; }

		virtual bool is_linear() const override { return true; }

		std::string name() const override { return "LinearElasticity"; }
		std::map<std::string, ParamFunc> parameters() const override;

		void assign_stress_tensor(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, const int all_size, const ElasticityTensorType &type, Eigen::MatrixXd &all, const std::function<Eigen::MatrixXd(const Eigen::MatrixXd &)> &fun) const override;

	private:
		// class that stores and compute lame parameters per point
		LameParameters params_;

		void compute_dstress_dgradu_multiply_mat(const int el_id, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &global_pts, const Eigen::MatrixXd &grad_u_i, const Eigen::MatrixXd &mat, Eigen::MatrixXd &stress, Eigen::MatrixXd &result) const;
		void compute_dstress_dmu_dlambda(const int el_id, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &global_pts, const Eigen::MatrixXd &grad_u_i, Eigen::MatrixXd &dstress_dmu, Eigen::MatrixXd &dstress_dlambda) const;

		// aux function that computes energy
		// double compute_energy is the same with T=double
		// assemble_gradient is the same with T=DScalar1 and return .getGradient()
		template <typename T>
		T compute_energy_aux(const NonLinearAssemblerData &data) const;
	};
} // namespace polyfem::assembler
