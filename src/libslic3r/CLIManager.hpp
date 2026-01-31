#ifndef ORCASLICER_CLIMANAGER_HPP
#define ORCASLICER_CLIMANAGER_HPP
namespace Slic3r {
namespace GUI {
class OpenGLManager;
}

struct CLIManager
{
    static bool is_cli_mode;
    static GUI::OpenGLManager* m_opengl_mgr;
};

}

#endif // ORCASLICER_CLIMANAGER_HPP
