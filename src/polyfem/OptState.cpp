#include "OptState.hpp"

#include <polyfem/solver/Optimizations.hpp>
#include <polyfem/utils/StringUtils.hpp>
#include <polyfem/utils/par_for.hpp>

#include <polyfem/solver/AdjointNLProblem.hpp>
#include <polyfem/solver/forms/adjoint_forms/VariableToSimulation.hpp>

#include <polysolve/nonlinear/Solver.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>

namespace polyfem
{
	OptState::OptState()
	{
	}

	void OptState::init_logger(
		const std::string &log_file,
		const spdlog::level::level_enum log_level,
		const spdlog::level::level_enum file_log_level,
		const bool is_quiet)
	{
		std::vector<spdlog::sink_ptr> sinks;

		if (!is_quiet)
		{
			console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			sinks.emplace_back(console_sink_);
		}

		if (!log_file.empty())
		{
			file_sink_ = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, /*truncate=*/true);
			// Set the file sink separately from the console so it can save all messages
			file_sink_->set_level(file_log_level);
			sinks.push_back(file_sink_);
		}

		init_logger(sinks, log_level);
		spdlog::flush_every(std::chrono::seconds(3));
	}

	void OptState::init_logger(std::ostream &os, const spdlog::level::level_enum log_level)
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.emplace_back(std::make_shared<spdlog::sinks::ostream_sink_mt>(os, false));
		init_logger(sinks, log_level);
	}

	void OptState::init_logger(
		const std::vector<spdlog::sink_ptr> &sinks,
		const spdlog::level::level_enum log_level)
	{
		set_adjoint_logger(std::make_shared<spdlog::logger>("adjoint-polyfem", sinks.begin(), sinks.end()));

		// Set the logger at the lowest level, so all messages are passed to the sinks
		adjoint_logger().set_level(spdlog::level::trace);
		set_log_level(log_level);
	}

	void OptState::set_log_level(const spdlog::level::level_enum log_level)
	{
		adjoint_logger().set_level(log_level);
		if (console_sink_)
			console_sink_->set_level(log_level); // Shared by all loggers
	}

	void OptState::init(const json &p_args_in, const bool strict_validation)
	{
		json args_in = p_args_in; // mutable copy
		args = solver::AdjointOptUtils::apply_opt_json_spec(args_in, strict_validation);

		// Save output directory and resolve output paths dynamically
		const std::string output_dir = utils::resolve_path(this->args["output"]["directory"], root_path(), false);
		if (!output_dir.empty())
		{
			std::filesystem::create_directories(output_dir);
		}
		this->output_dir = output_dir;

		std::string out_path_log = this->args["output"]["log"]["path"];
		if (!out_path_log.empty())
		{
			out_path_log = utils::resolve_path(out_path_log, root_path(), false);
		}

		init_logger(
			out_path_log,
			this->args["output"]["log"]["level"],
			this->args["output"]["log"]["file_level"],
			this->args["output"]["log"]["quiet"]);

		adjoint_logger().info("Saving adjoint output to {}", output_dir);

		const unsigned int thread_in = this->args["solver"]["max_threads"];
		set_max_threads(thread_in <= 0 ? std::numeric_limits<unsigned int>::max() : thread_in);
	}

	void OptState::set_max_threads(const unsigned int max_threads)
	{
		const unsigned int num_threads = std::max(1u, std::min(max_threads, std::thread::hardware_concurrency()));
		utils::NThread::get().num_threads = num_threads;
#ifdef POLYFEM_WITH_TBB
		thread_limiter = std::make_shared<tbb::global_control>(tbb::global_control::max_allowed_parallelism, num_threads);
#endif
		Eigen::setNbThreads(num_threads);
	}

	void OptState::create_states(const spdlog::level::level_enum &log_level, const int max_threads)
	{
		states = solver::AdjointOptUtils::create_states(
			args["states"],
			polyfem::solver::CacheLevel::Derivatives,
			log_level, max_threads <= 0 ? std::numeric_limits<unsigned int>::max() : max_threads);
	}

	void OptState::init_variables()
	{
		/* DOFS */
		ndof = 0;
		std::vector<int> variable_sizes;
		for (const auto &arg : args["parameters"])
		{
			int size = solver::AdjointOptUtils::compute_variable_size(arg, states);
			ndof += size;
			variable_sizes.push_back(size);
		}

		/* variable to simulations */
		variable_to_simulations.clear();
		for (const auto &arg : args["variable_to_simulation"])
			variable_to_simulations.push_back(
				solver::AdjointOptUtils::create_variable_to_simulation(
					arg,
					states,
					variable_sizes));
	}

	void OptState::crate_problem()
	{
		/* forms */
		std::shared_ptr<solver::AdjointForm> obj = solver::AdjointOptUtils::create_form(
			args["functionals"], variable_to_simulations, states);

		/* stopping conditions */
		std::vector<std::shared_ptr<solver::AdjointForm>> stopping_conditions;
		for (const auto &arg : args["stopping_conditions"])
			stopping_conditions.push_back(
				solver::AdjointOptUtils::create_form(arg, variable_to_simulations, states));

		nl_problem = std::make_shared<solver::AdjointNLProblem>(
			obj, stopping_conditions, variable_to_simulations, states, args);
	}

	void OptState::initial_guess(Eigen::VectorXd &x)
	{
		x = solver::AdjointOptUtils::inverse_evaluation(args["parameters"], ndof, variable_sizes, variable_to_simulations);

		for (auto &v2s : variable_to_simulations)
			v2s->update(x);
	}

	double OptState::eval(Eigen::VectorXd &x) const
	{

		nl_problem->solution_changed(x);
		return nl_problem->value(x);
	}

	void OptState::solve(Eigen::VectorXd &x)
	{
		auto nl_solver = solver::AdjointOptUtils::make_nl_solver(
			args["solver"]["nonlinear"],
			args["solver"]["linear"],
			args["solver"]["advanced"]["characteristic_length"]);
		nl_solver->minimize(*nl_problem, x);
	}
} // namespace polyfem