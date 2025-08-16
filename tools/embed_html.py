# pyright: reportUndefinedVariable=false

import os

def html_to_header(input_path, output_path, var_name):
    with open(input_path, "r", encoding="utf-8") as f:
        content = f.read()

    header_content = f'const char {var_name}[] PROGMEM = R"rawliteral(\n{content}\n)rawliteral";\n'

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    print(f"[HTML Embed] {input_path} -> {output_path}")

def before_build(source, target, env):
    project_dir = env['PROJECT_DIR']
    html_dir = os.path.join(project_dir, "html")
    src_dir = os.path.join(project_dir, "src")

    files = [
        ("index.html", "html_index.h", "index_html"),
        ("config.html", "html_config.h", "config_html"),
    ]

    for html_file, header_file, var_name in files:
        html_path = os.path.join(html_dir, html_file)
        header_path = os.path.join(src_dir, header_file)
        if os.path.exists(html_path):
            html_to_header(html_path, header_path, var_name)
        else:
            print(f"[WARNING] Missing HTML file: {html_file}")

# Hook into PlatformIO build system
Import("env")
before_build(None, None, env)
