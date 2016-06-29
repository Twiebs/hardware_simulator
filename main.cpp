#define QUICKAPP_IMPLEMENTATION
#define QUICKAPP_IMGUI
#define QUICKAPP_RENDER
#include "QuickApp.h"

#include "imgui_node_graph_test.cpp"

#include <vector>

enum LogicNodeType {
  LogicNodeType_AND,
  LogicNodeType_OR,
  LogicNodeType_XOR,
  LogicNodeType_COUNT,
};

static const char *LogicNodeName[] = {
  "AND",
  "OR",
  "XOR",
};

struct NodeConnection {
  uint32_t node_index;
  uint32_t slot_index;
};

struct EditorNode {
  LogicNodeType type;
  ImVec2 position;
  ImVec2 size;
  uint32_t outputID;
  uint32_t outputInputIndex;
  std::vector<NodeConnection> inputs;
  std::vector<NodeConnection> outputs;

    ImVec2 GetInputSlotPos(int slot_no) const   { return ImVec2(position.x, position.y + size.y * ((float)slot_no+1) / ((float)2+1)); }
  ImVec2 GetOutputSlotPos(int slot_no) const  { return ImVec2(position.x + size.x, position.y + size.y * ((float)slot_no+1) / ((float)1+1)); }
};

struct Editor {
  ImVector<EditorNode> nodes;
};

#define INVALID_NODE_ID UINT32_MAX

void CreateNode(LogicNodeType type, Editor *editor){
  EditorNode node = {};
  node.type = type;
  editor->nodes.push_back(node);
}

void DrawEditor(Editor *editor){
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
  ImGui::SetNextWindowSize(ImVec2(1280, 720));
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  if(!ImGui::Begin("Example: Custom Node Graph", 0, flags)){
    ImGui::End();
    return;
  }

    static ImVec2 scrolling = ImVec2(0.0f, 0.0f);
    static bool show_grid = true;
    static int node_selected = -1;

    // Draw a list of nodes on the left side
    bool open_context_menu = false;
    int node_hovered_in_list = -1;
    int node_hovered_in_scene = -1;
    ImGui::BeginChild("node_list", ImVec2(100,0));
    ImGui::Text("Nodes");
    ImGui::Separator();

    for (int node_idx = 0; node_idx < editor->nodes.size(); node_idx++){
      ImGui::PushID(node_idx);
      EditorNode *node = &editor->nodes[node_idx];
      if(ImGui::Selectable(LogicNodeName[node->type], node_idx == node_selected)){
        node_selected = node_idx;
      }
        
      if (ImGui::IsItemHovered()){
        node_hovered_in_list = node_idx;
        open_context_menu |= ImGui::IsMouseClicked(1);
      }
      ImGui::PopID();  
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();

    const float NODE_SLOT_RADIUS = 8.0f;
    const ImVec2 NODE_WINDOW_PADDING(16.0f, 16.0f);

    // Create our child canvas
    ImGui::Text("Hold middle mouse button to scroll (%.2f,%.2f)", scrolling.x, scrolling.y);
    ImGui::SameLine(ImGui::GetWindowWidth()-100);
    ImGui::Checkbox("Show grid", &show_grid);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60,60,70,200));
    ImGui::BeginChild("scrolling_region", ImVec2(0,0), true, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoMove);
    ImGui::PushItemWidth(120.0f);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImVec2 offset = ImGui::GetCursorScreenPos() - scrolling;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(2);

    // Display grid
    if (show_grid)
    {
        ImU32 GRID_COLOR = ImColor(200,200,200,40);
        float GRID_SZ = 64.0f;
        ImVec2 win_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImGui::GetWindowSize();
        for (float x = fmodf(offset.x,GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
            draw_list->AddLine(ImVec2(x,0.0f)+win_pos, ImVec2(x,canvas_sz.y)+win_pos, GRID_COLOR);
        for (float y = fmodf(offset.y,GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
            draw_list->AddLine(ImVec2(0.0f,y)+win_pos, ImVec2(canvas_sz.x,y)+win_pos, GRID_COLOR);
    }

    // Display links
    draw_list->ChannelsSetCurrent(0); // Background
    for(size_t i = 0; i < editor->nodes.size(); i++){
      EditorNode *a = &editor->nodes[i];
      for(size_t n = 0; n < a->outputs.size(); n++){
        ImVec2 p1 = offset + a->GetOutputSlotPos(n);
        EditorNode *b = &editor->nodes[a->outputs[n].node_index];
        ImVec2 p2 = offset + b->GetInputSlotPos(a->outputs[n].slot_index);
        draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
      }
    }

    static uint32_t dragNodeID = 0;
    static EditorNode *dragingNode = 0;
    static bool isDragSlotInput = 0;
    static int dragSlotIndex = 0;
    if(dragingNode != 0 && ImGui::IsMouseClicked(1)){
      dragingNode = 0;
    }


    // Display nodes
    for(size_t i = 0; i < editor->nodes.size(); i++){
      ImGui::PushID(i);
      EditorNode *node = &editor->nodes[i];
      
      ImVec2 node_rect_min = offset + node->position;
      draw_list->ChannelsSetCurrent(1); // Foreground
      ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);
      ImGui::BeginGroup(); // Lock horizontal position
      ImGui::Text(LogicNodeName[node->type]);
      ImGui::EndGroup();

      node->size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
      ImVec2 node_rect_max = node_rect_min + node->size;

      // Display node box
      draw_list->ChannelsSetCurrent(0); // Background
      ImGui::SetCursorScreenPos(node_rect_min);
      ImGui::InvisibleButton("node", node->size);
      
      if(ImGui::IsItemHovered()){
        node_hovered_in_scene = i;
        open_context_menu |= ImGui::IsMouseClicked(1);

        //@Create New @Node @Links @Dragging
        if(ImGui::IsMouseClicked(0)){
          static int nodeInputCount = 2;
          static int nodeOutputCount = 1;
          auto IsSlotPressed = [&](int slotIndex, bool isInputSlot){
            const float NODE_SLOT_RADIUS_SQUARED = NODE_SLOT_RADIUS*NODE_SLOT_RADIUS;
            ImVec2 canvasMouseCoords = ImGui::GetMousePos() - canvasOrigin;
            ImVec2 deltaSlot = isInputSlot ? node->GetInputSlotPos(slotIndex) : node->GetOutputSlotPos(slotIndex);
            deltaSlot -= canvasMouseCoords; 
            deltaSlot.x = abs(deltaSlot.x);
            deltaSlot.y = abs(deltaSlot.y);
            if((deltaSlot.x*deltaSlot.x)+(deltaSlot.y*deltaSlot.y) < NODE_SLOT_RADIUS_SQUARED){
              //Set mouse dragging mode
              if(dragingNode == 0){
                dragNodeID = i;
                dragingNode = node;
                dragSlotIndex = slotIndex;
                isDragSlotInput = isInputSlot;
              } else if(dragNodeID != i) { //Set new node links
                NodeConnection connection = {};
                connection.node_index = i;
                connection.slot_index = slotIndex;
                dragingNode->outputs.push_back(connection);
                dragingNode = 0;
              }
            }
          };


          for(size_t i = 0; i < nodeInputCount; i++){
            IsSlotPressed(i, 1);
          }

          for(size_t i = 0; i < nodeOutputCount; i++){
            IsSlotPressed(i, 0);
          }
        }
      }


      bool node_moving_active = ImGui::IsItemActive();
      if(node_moving_active)
        node_selected = i;
      if(node_moving_active && ImGui::IsMouseDragging(0))
        node->position = node->position + ImGui::GetIO().MouseDelta;

      ImU32 node_bg_color = (node_hovered_in_list == i || node_hovered_in_scene == i || (node_hovered_in_list == -1 && node_selected == i)) ? ImColor(75,75,75) : ImColor(60,60,60);
      draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 4.0f); 
      draw_list->AddRect(node_rect_min, node_rect_max, ImColor(100,100,100), 4.0f); 
      for(int slot_idx = 0; slot_idx < 2; slot_idx++)
        draw_list->AddCircleFilled(offset + node->GetInputSlotPos(slot_idx), NODE_SLOT_RADIUS, ImColor(150,150,150,150));
      for(int slot_idx = 0; slot_idx < 1; slot_idx++)
        draw_list->AddCircleFilled(offset + node->GetOutputSlotPos(slot_idx), NODE_SLOT_RADIUS, ImColor(150,150,150,150));

      ImGui::PopID();
    }
    draw_list->ChannelsMerge();

    //@Dragging 
    if(dragingNode != 0){
      ImVec2 p1 = ImGui::GetMousePos();
      ImVec2 p2 = offset + (isDragSlotInput ? dragingNode->GetInputSlotPos(dragSlotIndex) : dragingNode->GetOutputSlotPos(dragSlotIndex));
      draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
    }



#if 1
    // Open context menu
    if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1)){
      node_selected = node_hovered_in_list = node_hovered_in_scene = -1;
      open_context_menu = true;
    }
#endif

    #if 1
    if (open_context_menu){
      ImGui::OpenPopup("context_menu");
      if (node_hovered_in_list != -1)
        node_selected = node_hovered_in_list;
      if (node_hovered_in_scene != -1)
        node_selected = node_hovered_in_scene;
    }
    #endif

#if 1
    // Draw context menu
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
    if (ImGui::BeginPopup("context_menu"))
    {
        ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
        EditorNode* node = node_selected != -1 ? &editor->nodes[node_selected] : NULL;

        if(node) {
            //ImGui::Text("Node '%s'", node->Name);
            
            ImGui::Separator();
            //if (ImGui::MenuItem("Rename..", NULL, false, false)) {}
            //if (ImGui::MenuItem("Delete", NULL, false, false)) {}
            //if (ImGui::MenuItem("Copy", NULL, false, false)) {}
        }
        else
        {
          for(size_t i = 0; i < LogicNodeType_COUNT; i++){
            if(ImGui::MenuItem(LogicNodeName[i])) {}
          }


            //if (ImGui::MenuItem("Add")) { nodes.push_back(Node(nodes.Size, "New node", scene_pos, 0.5f, ImColor(100,100,200), 2, 2)); }
            //if (ImGui::MenuItem("Paste", NULL, false, false)) {}
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    #endif

    // Scrolling
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
        scrolling = scrolling - ImGui::GetIO().MouseDelta;

    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();

    ImGui::End();
}


int main(){
  QuickAppStart("Hardware Simulator", 1280, 720);
  
  Editor editor = {};
  CreateNode(LogicNodeType_AND, &editor);
  CreateNode(LogicNodeType_OR, &editor);
  CreateNode(LogicNodeType_XOR, &editor);

  QuickAppLoop([&]() {
    DrawEditor(&editor);
  });
}