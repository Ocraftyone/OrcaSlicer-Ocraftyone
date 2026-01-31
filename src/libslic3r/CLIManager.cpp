#include "CLIManager.hpp"

namespace Slic3r {
bool CLIManager::is_cli_mode = false;
GUI::OpenGLManager* CLIManager::m_opengl_mgr = nullptr;
}