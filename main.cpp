#include <iostream>
#include <cassert>
#include <ncurses.h>
#include <thread>
#include <algorithm>
#include <mutex>
#include <filesystem>
#include <utility>

#include "CallgrindParser.hpp"

std::string short_path(const std::string &f) {
  namespace fs = std::filesystem;
  fs::path p(f);
  return p.filename();
}

struct ListView {
  explicit ListView(std::shared_ptr<CallgrindParser> parser) : parser(std::move(parser)) {}
  ~ListView() {
    delwin(window);
  }

 public:
  enum NameViewMode {
    kObject, kFilename
  };
  enum CostViewMode {
    kRelative, kAbsolute
  };

  void destroy() {
    delwin(window);
  }

  void render() {
    render_lock.lock();
    auto height = LINES - 1;
    auto width = COLS - 1;
    if (!window) {
      window = newwin(height, width, 1, 1);
    } else {
      height = getmaxy(window);
      width = getmaxx(window);
    }

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);

    wclear(window);
    box(window, 0, 0);

    auto actual_width = width - 2;
    const auto nlines = getNumberOfLines();

    if (!parser || parser->getEntries().empty()) {
      wrefresh(window);
      render_lock.unlock();
      return;
    }

    auto &entries = parser->getEntries();

    int ientry = entry_offset;
    auto max_cost = entries[0]->totalCost()[0];
    for (int iline = 1; iline <= nlines; ++iline) {
      if (ientry < entries.size()) {
        const auto &entry = entries.at(ientry);
        auto entry_cost = entry->totalCost()[0];
        if (iline == selected_line) {
          wattron(window, COLOR_PAIR(2));
        } else {
          wattron(window, COLOR_PAIR(1));
        }

        std::string position = name_repr == kObject ? entry->position->binary : entry->position->source;
        std::string function = entry->position->symbol;
        std::string delim = " : ";

        std::stringstream costs_stream;
        if (costs_repr == kRelative) {
          costs_stream << std::setw(10) << std::setprecision(2) << 100 * double(entry_cost) / max_cost << "%";
        } else {
          costs_stream << std::setw(10) << std::setprecision(4) << double(entry_cost);
        }
        std::string costs = costs_stream.str();

        auto name = short_path(position) + "/" + function;
        auto max_name_width = actual_width - delim.size() - costs.size();
        if (name.size() > max_name_width) {
          if (iline == selected_line) {
            auto max_line_offset = name.size() - max_name_width;
            if (selected_line_offset > max_line_offset) {
              selected_line_offset = max_line_offset;
            }
            name = name.substr(selected_line_offset, max_name_width);
            if (selected_line_offset < max_line_offset) {
              name[name.length() - 1] = '>';
            }
          } else {
            name = name.substr(0, actual_width - 3 - costs.size());
            name[name.length() - 1] = '>';
          }
          if (iline == selected_line && selected_line_offset > 0) {
            name[0] = '<';
          }
        }

        mvwprintw(
            window,
            iline, 1,
            "%s%s%s",
            costs_stream.str().c_str(),
            delim.c_str(),
            name.c_str());
        lastline = iline;
      } else {
        break;
      }
      ientry++;
    }
    wattron(window, COLOR_PAIR(1));
    wrefresh(window);
    render_lock.unlock();
  }

  void shiftSelection(int pos) {
    selected_line += pos;
    if (entry_offset + selected_line >= parser->getEntries().size()) {
      selected_line = parser->getEntries().size() - entry_offset;
    } else if (selected_line >= lastline) {
      selected_line = lastline - 1;
      if (entry_offset < parser->getEntries().size() - 1) entry_offset++;
    } else if (selected_line < 1) {
      selected_line = 1;
      if (entry_offset > 0) entry_offset--;
    }
    selected_line_offset = 0;
    render();
  }

  void shiftPage(int d) {
    if (d > 0) {
      if (entry_offset + getNumberOfLines() >= parser->getEntries().size()) {
        /* do nothing */
      } else {
        entry_offset += getNumberOfLines();
        if (entry_offset + selected_line >= parser->getEntries().size()) {
          selected_line = parser->getEntries().size() - entry_offset;
        }
      }
    } else if (d < 0) {
      if (entry_offset - getNumberOfLines() < 0) {
        entry_offset = 0;
      } else {
        entry_offset -= getNumberOfLines();
      }
    }
    selected_line_offset = 0;
    render();
  }

  void shiftSelectedLineOffset(int pos) {
    if (pos < 0 && selected_line_offset < -pos) {
      selected_line_offset = 0;
    } else {
      selected_line_offset += pos;
    }
    render();
  }

  void resetSelectedLineOffset() {
    selected_line_offset = 0;
    render();
  }

  void toggleNameRepr() {
    if (name_repr == kObject)
      name_repr = kFilename;
    else
      name_repr = kObject;
    render();
  }

  void toggleCostsRepr() {
    if (costs_repr == kRelative)
      costs_repr = kAbsolute;
    else
      costs_repr = kRelative;
    render();
  }

  int dispatch(int ch) {
    switch (ch) {
      case 'j':
      case KEY_DOWN:shiftSelection(1);
        break;
      case 'k':
      case KEY_UP:shiftSelection(-1);
        break;
      case 'l':
      case KEY_RIGHT: shiftSelectedLineOffset(1);
        break;
      case 'h':
      case KEY_LEFT: shiftSelectedLineOffset(-1);
        break;
      case '^':
      case KEY_HOME: resetSelectedLineOffset();
        break;
      case KEY_NPAGE:
      case 'f':shiftPage(1);
        break;
      case KEY_PPAGE:
      case 'b':shiftPage(-1);
        break;
      case 'F':toggleNameRepr();
        break;
      case 'C':toggleCostsRepr();
        break;
      default:;
    }
    return 0;
  }

 private:
  inline int getNumberOfLines() const {
    return getmaxy(window) - 2;
  }

  std::mutex render_lock;
  WINDOW *window{nullptr};
  std::shared_ptr<CallgrindParser> parser{};

  int name_repr{0};
  int costs_repr{kRelative};
  int entry_offset{0};
  size_t selected_line_offset{0};
  size_t selected_line{1};
  int lastline{1};
};

struct TreeView {

  enum CostsView {
    kAbsolute,
    kPersentage
  };
  enum ENameView {
    kSymbolOnly,
    kFileSymbol,
    kObjectSymbol
  };

  struct TreeNode {
    int level{0};
    bool expandable{true};
    bool selectable{true};
    std::vector<std::shared_ptr<TreeNode>> children;
    std::function<std::string(int, int)> render_string;
    std::function<void()> on_expand;

    bool is_expanded{false};
    bool is_selected{false};
  };

  using TreeNodePtr = std::shared_ptr<TreeNode>;

  explicit TreeView(std::shared_ptr<CallgrindParser> parser) : parser(std::move(parser)) {
  }

  void render() {
    static std::string expand_symbol = "[+]";
    static std::string collapse_symbol = "[-]";
    static std::string nonexpandable_symbol = " * ";

    auto height = LINES - 1;
    auto width = COLS - 1;
    if (!window) {
      window = newwin(height, width, 1, 1);
    }
    else if (
        getmaxx(window) != COLS-1 ||
            getmaxy(window) != LINES-1) {
      delwin(window);
      window = newwin(height, width, 1, 1);
    }
    else {
      height = getmaxy(window);
      width = getmaxx(window);
    }

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_WHITE);

    wclear(window);
    box(window, 0, 0);

    auto actual_width = width - 2;
    auto actual_height = height - 1;

    if (!nodes_initialized) {
      initNodes();
      auto first_selectable_it = std::find_if(begin(nodes), end(nodes), [] (TreeNodePtr& nodeptr) {
        return nodeptr->selectable;
      });
      selected_inode = std::distance(begin(nodes), first_selectable_it);
      nodes[selected_inode]->is_selected = true;
      nodes_initialized = true;
    }

    if (selected_inode - offset_inode >= actual_height - 2) {
      offset_inode = selected_inode - (actual_height - 2);
    }
    else if (selected_inode < offset_inode) {
      offset_inode = selected_inode;
    }
    int inode = offset_inode;
    assert(inode >= 0);
    for (int iline = 1; iline < actual_height && inode < nodes.size(); ++iline, ++inode) {
      auto &node = *nodes[inode];
      std::stringstream line_text;

      std::string bullet_symbol = node.expandable?
          node.is_expanded? collapse_symbol : expand_symbol :
          nonexpandable_symbol;

      line_text << node.render_string(0,0);

      auto text = line_text.str();
      auto node_offset = 1 + 2*node.level;
      mvwprintw(window, iline, node_offset, "%s", bullet_symbol.c_str());
      if (node.is_selected) {
        wattron(window, COLOR_PAIR(2));
      }
      mvwprintw(window, iline, node_offset + 1 + bullet_symbol.length(), "%s", text.c_str());
      wattron(window, COLOR_PAIR(1));
    }

    wrefresh(window);
  }

  int dispatch(int ch) {
    switch (ch) {
      case 'e':
      case 'l':
      case KEY_RIGHT:expand_selected();
        break;
      case 'h':
      case KEY_LEFT:collapse_selected();
        break;
      case 'j':
      case KEY_DOWN:
      case 'n':next_selectable();
        break;
      case 'k':
      case KEY_UP:
      case 'p':prev_selectable();
        break;
      case 'v':
        toggleNameView();
        break;
      default:;
    }
    return 0;
  }

  void destroy() {

  }

 private:
  void expand_selected() {
    auto current_node = nodes[selected_inode];
    if (!current_node->expandable) {
      return;
    }
    if (current_node->is_expanded)
      return;
    if (current_node->on_expand) {
      current_node->on_expand();
    }
    current_node->is_expanded = true;
    for (auto &child : current_node->children) {
      child->level = current_node->level + 1;
    }
    nodes.insert(begin(nodes) + selected_inode + 1,
                 begin(current_node->children),
                 end(current_node->children));
    render();
  }

  void collapse_selected() {
    auto current_node = nodes[selected_inode];
    if (!current_node->is_expanded) /* already collapsed */
      return;
    current_node->is_expanded = false;
    auto current_node_it = begin(nodes) + selected_inode;
    auto collapse_begin = current_node_it + 1;
    auto collapse_end = std::find_if(collapse_begin, end(nodes), [&current_node](TreeNodePtr ptr) {
      return current_node->level >= ptr->level;
    });
    nodes.erase(collapse_begin, collapse_end);
    render();
  }


  TreeNodePtr
  makeCallNode(const CallgrindParser::Call& call) {

    auto new_node = std::make_shared<TreeNode>();
    new_node->expandable = true;
    new_node->selectable = true;
    new_node->render_string = [this, new_node, call] (int, int) {
      std::stringstream text_stream;
      text_stream << "[calls=" << std::setprecision(2) << double(call.ncalls) << "] ";
      text_stream << "[Ir=" << std::setprecision(2) << double(call.totalCosts()[0]) << "] ";
      if (name_view == kSymbolOnly) {
        text_stream << call.entry->position->symbol;
      } else if (name_view == kFileSymbol) {
        text_stream << short_path(call.entry->position->source) << ":::" << call.entry->position->symbol;
      } else if (name_view == kObjectSymbol) {
        text_stream << short_path(call.entry->position->binary) << ":::" << call.entry->position->symbol;
      }
      return text_stream.str();
    };
    auto call_entry = call.entry;
    if (call_entry->calls.empty()) {
      new_node->expandable = false;
      return new_node;
    }
    new_node->on_expand = [this,call_entry, new_node]() {
      new_node->children.clear();
      auto calls = call_entry->calls;
      std::sort(begin(calls), end(calls), [] (const CallgrindParser::Call& lhs, const CallgrindParser::Call& rhs) {
        return lhs.totalCosts()[0] > rhs.totalCosts()[0];
      });
      for (auto &call : calls) {
        new_node->children.emplace_back(makeCallNode(call));
      }
    };
    return new_node;
  }


  TreeNodePtr
  makeEntryNode(const CallgrindParser::EntryPtr &entry) {

    auto new_node = std::make_shared<TreeNode>();
    new_node->expandable = true;
    new_node->selectable = true;
    new_node->is_expanded = false;

    new_node->render_string = [this, entry, new_node] (int , int) -> std::string {
      std::stringstream text_stream;
      text_stream << "[" << std::setw(7) << std::setprecision(2) << double(entry->totalCost()[0]) << "] ";
      if (name_view == kSymbolOnly) {
        text_stream << entry->position->symbol;
      } else if (name_view == kFileSymbol) {
        text_stream << short_path(entry->position->source) << ":::" << entry->position->symbol;
      } else if (name_view == kObjectSymbol) {
        text_stream << short_path(entry->position->binary) << ":::" << entry->position->symbol;
      }
      return text_stream.str();
    };

    if (entry->calls.empty()) {
      new_node->expandable = false;
      new_node->is_expanded = false;
      return new_node;
    }

    new_node->on_expand = [this,entry, new_node]() {
      new_node->children.clear();
      auto calls = entry->calls;
      std::sort(begin(calls), end(calls), [] (const CallgrindParser::Call& lhs, const CallgrindParser::Call& rhs) {
        return lhs.totalCosts()[0] > rhs.totalCosts()[0];
      });
      for (auto &call : calls) {
        new_node->children.emplace_back(makeCallNode(call));
      }
    };
    return new_node;
  }

  void initNodes() {
    auto &entries = parser->getEntries();

    for (auto &entry : entries) {
      nodes.emplace_back(makeEntryNode(entry));
    }
  }

  void next_selectable() {
    auto current_node_it = begin(nodes) + selected_inode;
    auto next_selectable_node_it = std::find_if(current_node_it + 1, end(nodes), [](const TreeNodePtr &n) {
      return n->selectable;
    });
    if (next_selectable_node_it == end(nodes)) {
      /* ignore */
    } else {
      nodes[selected_inode]->is_selected = false;
      selected_inode = std::distance(begin(nodes), next_selectable_node_it);
      nodes[selected_inode]->is_selected = true;
    }
    render();
  }

  void prev_selectable() {
    if (selected_inode == 0) {
      return;
    }
    auto current_node_rit = rbegin(nodes) + (nodes.size() - 1 - selected_inode);
    auto prev_selectable_node_it = std::find_if(current_node_rit + 1, rend(nodes), [](const TreeNodePtr &n) {
      return n->selectable;
    });
    if (prev_selectable_node_it == rend(nodes)) {
      /* ignore */
    } else {
      nodes[selected_inode]->is_selected = false;
      selected_inode = nodes.size() - std::distance(rbegin(nodes), prev_selectable_node_it) - 1;
      nodes[selected_inode]->is_selected = true;
    }

    render();
  }

  void toggleNameView() {
    if (name_view == kSymbolOnly) {
      name_view = kFileSymbol;
    } else if (name_view == kFileSymbol) {
      name_view = kObjectSymbol;
    } else if (name_view == kObjectSymbol) {
      name_view = kSymbolOnly;
    }
    render();
  }

  ENameView name_view{kSymbolOnly};
  int selected_inode{0};
  int offset_inode{0};

  WINDOW *window{nullptr};
  std::shared_ptr<const CallgrindParser> parser{};

  bool nodes_initialized{false};
  std::vector<std::shared_ptr<TreeNode>> nodes;
};

int main(int argc, char *argv[]) {
  if (argc == 1)
    return 1;

  std::string file_to_process{argv[1]};

  initscr();            /* Start curses mode 		*/

  if (!has_colors()) {
    std::cerr << "Colors are not supported by your terminal" << std::endl;
    exit(1);
  }
  start_color();

  cbreak();            /* Line buffering disabled, Pass on
					 * everty thing to me 		*/
  keypad(stdscr, TRUE);        /* I need that nifty F1 	*/
  /* param 1     : color name
  * param 2, 3, 4 : rgb content min = 0, max = 1000 */
//  assert(init_color(color_test, 500, 500, 500) != ERR);

  noecho();

  printw("Press F10 to exit");
  refresh();

  auto parser = std::make_shared<CallgrindParser>(file_to_process);
  auto list_view = std::make_shared<ListView>(parser);
  auto tree_view = std::make_shared<TreeView>(parser);
  parser->SetVerbose(false);
  parser->parse();

//  list_view->render();
//  std::thread parse_thread([](
//      std::shared_ptr<CallgrindParser> parser,
//      std::shared_ptr<ListView> entries_view
//      ) {
//    parser->parse();
//    entries_view->render();
//  }, parser, list_view);
//  parse_thread.detach();
//

//list_view->render();
  tree_view->render();

  int ch;
  while ((ch = getch()) != KEY_F(10)) {
//    list_view->dispatch(ch);
    tree_view->dispatch(ch);
  }

//  list_view->destroy();
  tree_view->destroy();
  endwin();            /* End curses mode		  */
  return 0;
}

