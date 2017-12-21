// A dummy GLUP application

#include "Navigation.hpp"
#include "MeshUtils.hpp"
#include "Singularities.hpp"
#include "Refinement.hpp"
#include <geogram/basic/file_system.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram_gfx/glup_viewer/glup_viewer.h>
#include <geogram_gfx/glup_viewer/glup_viewer_gui.h>
#include <geogram_gfx/mesh/mesh_gfx.h>
#include <Eigen/Dense>

namespace {
	using namespace std;
	using namespace GEO;
	using namespace poly_fem;

	Navigation::Index idx_;

	Eigen::VectorXi singular_vertices_;
	Eigen::MatrixX2i singular_edges_;

	class DemoGlupApplication : public SimpleMeshApplication {
	public:
		DemoGlupApplication(int argc, char** argv) :
			SimpleMeshApplication(argc, argv, "<filename>")
		{
			name_ = "[Float] Navigation";
		}

		virtual void init_graphics() override {
			SimpleMeshApplication::init_graphics();
			glup_viewer_disable(GLUP_VIEWER_BACKGROUND);
			//glup_viewer_disable(GLUP_VIEWER_3D);
			ImGui::GetStyle().WindowRounding = 7.0f;
			ImGui::GetStyle().FrameRounding = 0.0f;
			ImGui::GetStyle().GrabRounding = 0.0f;
			retina_mode_ = false;
			scaling_ = 1.0f;
		}

		virtual bool load(const std::string &filename) override {
			if(!GEO::FileSystem::is_file(filename)) {
				GEO::Logger::out("I/O") << "is not a file" << std::endl;
				return false;
			}
			SimpleMeshApplication::load(filename);

			// Compute mesh connectivity
			Navigation::prepare_mesh(mesh_);

			// Initialize the key
			idx_ = Navigation::get_index_from_face(mesh_, 0, 0);

			// Compute singularities
			poly_fem::singularity_graph(mesh_, singular_vertices_, singular_edges_);

			// Compute types
			std::vector<ElementType> tags;
			poly_fem::compute_element_tags(mesh_, tags);

			return true;
		}

		virtual void draw_viewer_properties() override {
			ImGui::Text("Vertex: %d", idx_.vertex);
			ImGui::Text("Edge:   %d", idx_.edge);
			ImGui::Text("Facet:  %d", idx_.face);
			idx_.vertex = std::max(0, std::min((int) mesh_.vertices.nb(), idx_.vertex));
			idx_.edge = std::max(0, std::min((int) mesh_.edges.nb(), idx_.edge));
			idx_.face = std::max(0, std::min((int) mesh_.facets.nb(), idx_.face));
			if (ImGui::Button("Switch Vertex", ImVec2(-1, 0))) {
				idx_ = Navigation::switch_vertex(mesh_, idx_);
			}
			if (ImGui::Button("Switch Edge", ImVec2(-1, 0))) {
				idx_ = Navigation::switch_edge(mesh_, idx_);
			}
			if (ImGui::Button("Switch Face", ImVec2(-1, 0))) {
				auto tmp = Navigation::switch_face(mesh_, idx_);
				if (tmp.face != -1) {
					idx_ = tmp;
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Kill Singularities", ImVec2(-1, 0))) {
				poly_fem::create_patch_around_singularities(mesh_, singular_vertices_, singular_edges_);
				Navigation::prepare_mesh(mesh_);
				poly_fem::singularity_graph(mesh_, singular_vertices_, singular_edges_);
				GEO::mesh_save(mesh_, "foo.obj");
			}

			if (ImGui::Button("Refine", ImVec2(-1, 0))) {
				GEO::Mesh tmp;
				poly_fem::refine_polygonal_mesh(mesh_, tmp);
				mesh_.copy(tmp);
				Navigation::prepare_mesh(mesh_);
				poly_fem::singularity_graph(mesh_, singular_vertices_, singular_edges_);
				GEO::mesh_save(mesh_, "foo.obj");
			}
		}

		virtual void draw_scene() override {
			if (mesh()) {
				draw_selected();
				draw_singular();
			}
			SimpleMeshApplication::draw_scene();
		}

		static vec3 mesh_vertex(const GEO::Mesh &M, int i) {
			if (M.vertices.single_precision()) {
				const float *p = M.vertices.single_precision_point_ptr(i);
				return vec3(p[0], p[1], p[2]);
			} else {
				return M.vertices.point(i);
			}
		}

		virtual void draw_selected() {
			glupSetPointSize(GLfloat(10));
			glupEnable(GLUP_VERTEX_COLORS);
			glupColor3f(1.0f, 0.0f, 0.0f);

			// Selected vertex
			glupBegin(GLUP_POINTS);
			glupVertex(mesh_vertex(mesh_, idx_.vertex));
			glupEnd();

			// Selected edge
			glupSetMeshWidth(5);
			glupColor3f(0.0f, 0.8f, 0.0f);
			glupBegin(GLUP_LINES);
			{
				int v0 = mesh_.edges.vertex(idx_.edge, 0);
				int v1 = mesh_.edges.vertex(idx_.edge, 1);
				glupVertex(mesh_vertex(mesh_, v0));
				glupVertex(mesh_vertex(mesh_, v1));
			}
			// Boundary edges
			GEO::Attribute<int> boundary(mesh_.edges.attributes(), "boundary_edge");
			glupColor3f(0.7f, 0.7f, 0.0f);
			for (int e = 0; e < (int) mesh_.edges.nb(); ++e) {
				if (boundary[e]) {
					int v0 = mesh_.edges.vertex(e, 0);
					int v1 = mesh_.edges.vertex(e, 1);
					// glupVertex(mesh_vertex(mesh_, v0));
					// glupVertex(mesh_vertex(mesh_, v1));
				}
			}
			glupEnd();

			// Selected facet
			glupSetMeshWidth(0);
			glupColor3f(1.0f, 0.0f, 0.0f);
			glupBegin(GLUP_TRIANGLES);
			for (int lv = 1; lv + 1 < (int) mesh_.facets.nb_vertices(idx_.face); ++lv) {
				int v0 = mesh_.facets.vertex(idx_.face, 0);
				int v1 = mesh_.facets.vertex(idx_.face, lv);
				int v2 = mesh_.facets.vertex(idx_.face, lv+1);
				glupVertex(mesh_vertex(mesh_, v0));
				glupVertex(mesh_vertex(mesh_, v1));
				glupVertex(mesh_vertex(mesh_, v2));
			}
			glupEnd();

			glupDisable(GLUP_VERTEX_COLORS);
		}

		void draw_singular() {
			glupSetPointSize(GLfloat(10));
			glupEnable(GLUP_VERTEX_COLORS);
			glupColor3f(0.0f, 0.0f, 0.8f);

			// Selected vertex
			glupBegin(GLUP_POINTS);
			for (int i = 0; i < singular_vertices_.size(); ++i) {
				glupVertex(mesh_vertex(mesh_, singular_vertices_[i]));
			}
			glupEnd();

			// Selected edge
			glupSetMeshWidth(5);
			glupBegin(GLUP_LINES);
			for (int e = 0; e < singular_edges_.rows(); ++e) {
				int v0 = singular_edges_(e, 0);
				int v1 = singular_edges_(e, 1);
				glupVertex(mesh_vertex(mesh_, v0));
				glupVertex(mesh_vertex(mesh_, v1));
			}
			glupEnd();
			glupDisable(GLUP_VERTEX_COLORS);
		}

	};
}

int main(int argc, char** argv) {
	#ifndef WIN32
	setenv("GEO_NO_SIGNAL_HANDLERS", "1", 1);
	#endif
	GEO::initialize();
	GEO::CmdLine::import_arg_group("standard");
	GEO::CmdLine::import_arg_group("algo");
	GEO::CmdLine::import_arg_group("gfx");
	DemoGlupApplication app(argc, argv);
	GEO::CmdLine::set_arg("gfx:geometry", "1024x1024");
	app.start();
	return 0;
}
