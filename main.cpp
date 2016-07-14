#define QUICKAPP_IMPLEMENTATION
#define QUICKAPP_IMGUI
#define QUICKAPP_RENDER
#include "QuickApp.h"

#pragma clang diagnostic ignored "-Wformat-security"

#include "utils.cpp"

#define it(name, n) for(size_t name = 0; name < n; name++)

enum NodeType {
  NodeType_INVALID,
  NodeType_AND,
  NodeType_OR,
  NodeType_XOR,
  NodeType_INPUT,
  NodeType_OUTPUT,
  NodeType_COUNT,
};

static const char *NodeName[] = {
  "INVALID",
  "AND",
  "OR",
  "XOR",
  "INPUT",
  "OUTPUT",
};

enum NodeState : uint8_t {
  NodeState_LOW,
  NodeState_HIGH,
  NodeState_NONE,
};

struct EditorNode;

struct NodeIndex {
  uint32_t node_index;
  EditorNode *node_ptr;
};

struct NodeConnection {
  NodeIndex node_index;
  uint32_t io_index;
};

struct Node {
  uint32_t type;
  size_t input_count;
  size_t output_count;
  NodeState *input_state;                           //NodeState[input_count]
  NodeConnection *inputConnections;     
};

struct EditorNode {
  uint32_t type;
  size_t input_count;
  size_t output_count;
  NodeState *input_state;                           //NodeState[input_count]
  
  NodeConnection *inputConnections;                 //NodeConnection[input_count]
  DynamicArray<NodeConnection> *output_connections; //DynamicArray<NodeConnection>[output_count]
 
  ImVec2 position;
  ImVec2 size;
  NodeState signal_state;
};

struct ICNodeConnection {
  uint32_t node_index;
  uint32_t io_index;
};

struct ICNode {
  uint32_t type;
  uint32_t input_count;
  uint32_t output_count;

  NodeState *input_state;
  uint32_t *connection_count_per_output;
  ICNodeConnection *output_connections;
};

struct ICDefinition {
  uint32_t input_count;
  uint32_t output_count;
  uint32_t node_count;
  ICNode *nodes; 
};

#include "editor.cpp"


EditorNode *AllocateNode(uint32_t input_count, uint32_t output_count) {
  size_t required_memory = sizeof(EditorNode);
  required_memory = (required_memory + 0x7) & ~0x7;
  required_memory += sizeof(NodeState) * input_count;
  required_memory = (required_memory + 0x3) & ~0x3;
  required_memory += sizeof(NodeConnection) * input_count;
  required_memory = (required_memory + 0x3) & ~0x3;
  required_memory += sizeof(DynamicArray<NodeConnection>) * output_count;

  EditorNode *node = (EditorNode *)malloc(required_memory);
  memset(node, 0, required_memory);
  node->input_count = input_count;
  node->output_count = output_count;

  uintptr_t current = (uintptr_t)(node + 1);
  current = (current + 0x7) & ~0x7;
  node->input_state = (NodeState *)current;
  current += sizeof(NodeState) * input_count;
  current = (current + 0x3) & ~0x3;
  node->inputConnections = (NodeConnection *)current;
  current += sizeof(NodeConnection) * input_count;
  current = (current + 0x3) & ~0x3;
  node->output_connections = (DynamicArray<NodeConnection> *)current;

  //TODO(Torin) Proper node sizing
  uint32_t largestIOCount = Max(input_count, output_count);
  node->size = ImVec2(64, 64);

  return node;
}

EditorNode *CreateNode(uint32_t node_type, Editor *editor){
  uint32_t input_count = 0, output_count = 0;
  switch(node_type){
    case NodeType_INPUT:{
      output_count = 1;
    }break;
    case NodeType_OUTPUT:{
      input_count = 1;
    }break;

    default:{
      if(node_type > NodeType_COUNT){
        uint32_t ic_index = node_type - (NodeType_COUNT + 1);
        ICDefinition *ic = editor->icdefs[ic_index];
        input_count = ic->input_count;
        output_count = ic->output_count;
      } else {
        input_count = 2;
        output_count = 1;
      }
    }break;

  }

  auto node = AllocateNode(input_count, output_count);
  node->type = node_type;
  ArrayAdd(node, editor->nodes);
  if(node->type == NodeType_INPUT){
    ArrayAdd(node, editor->inputs);
  }
  return node;
}


static inline
bool IsValid(NodeIndex index){
  bool result = index.node_ptr != nullptr;
  return result;
}

static inline
NodeIndex InvalidNodeIndex(){
  NodeIndex result;
  result.node_ptr = nullptr;
  return result;
}

static inline 
bool operator==(const NodeIndex& a, const NodeIndex& b){
  if(a.node_ptr != b.node_ptr) return 0;
  return 1;
}

static inline 
bool operator!=(const NodeIndex& a, const NodeIndex& b){
  if(a.node_ptr == b.node_ptr) return 0;
  return 1;
}

#if 0
static inline 
bool operator==(const NodeIndex& a, const NodeIndex& b){
  if(a.block_index != b.block_index) return 0;
  if(a.node_index != b.node_index) return 0;
  return 1;
}

static inline 
bool operator!=(const NodeIndex& a, const NodeIndex& b){
  if(a.block_index == b.block_index && a.node_index == b.node_index) return 0;
  return 1;
}
#endif

#if 0
static inline
EditorNode *GetNextAvailableNode(Editor *editor){
  for(size_t blockIndex = 0; blockIndex < editor->nodeBlocks.count; blockIndex++){
    NodeBlock *block = editor->nodeBlocks[blockIndex];
    if(block->currentOccupiedCount != NodeBlock::NODE_COUNT){
      for(size_t nodeIndex; nodeIndex < NodeBlock::NODE_COUNT; nodeIndex++){
        if(!block->isOccupied[nodeIndex]){
          block->isOccupied[nodeIndex] = 1;
          block->currentOccupiedCount++;
          return &block->nodes[nodeIndex];
        }
      }
    }
  }

  NodeBlock*block = (NodeBlock *)malloc(sizeof(NodeBlock));
  memset(block, 0, sizeof(NodeBlock));
  ArrayAdd(block, editor->nodeBlocks);
  return GetNextAvailableNode(editor);
}
#endif

static inline
EditorNode *GetNode(NodeIndex index, Editor *editor){
  return index.node_ptr;
}

static inline
void DeleteNode(NodeIndex index, Editor *editor){
  EditorNode *node = index.node_ptr;

  for(size_t i = 0; i < node->input_count; i++){
    NodeConnection *connection = &node->inputConnections[i];
    if(IsValid(connection->node_index) == false) continue;
    EditorNode *inputSource = GetNode(connection->node_index, editor);
    bool wasConnectionRemoved = false;

    DynamicArray<NodeConnection> &sourceConnections = inputSource->output_connections[connection->io_index];
    for(size_t n = 0; n < sourceConnections.count; n++){
      const NodeConnection *sourceConnection = &sourceConnections[n];
      if((sourceConnection->node_index == index) && sourceConnection->io_index == i){
        ArrayRemoveAtIndexUnordered(n, sourceConnections);
        wasConnectionRemoved = true;              
      }
    }
    assert(wasConnectionRemoved && "inputConnection did not match any outputConnection");
  }

  for(size_t outputIndex = 0; outputIndex < node->output_count; outputIndex++){
    DynamicArray<NodeConnection>& connections = node->output_connections[outputIndex];
    for(size_t i = 0; i < connections.count; i++){
      NodeConnection *connection = &connections[i];
      EditorNode *connectionDest = GetNode(connection->node_index, editor);
      connectionDest->inputConnections[connection->io_index].node_index = InvalidNodeIndex();
    }
  }

  for(size_t i = 0; i < node->output_count; i++){
    ArrayDestroy(node->output_connections[i]);
  }

  ArrayRemoveValueUnordered(node, editor->nodes);
  if(node->type == NodeType_INPUT){
    ArrayRemoveValueUnordered(node, editor->inputs);
  }

  free(node);


  //NodeBlock *block = editor->nodeBlocks[index.block_index];
  //block->isOccupied[index.node_index] = 0;
  //ArrayDestroy(block->nodes[index.node_index].outputConnections);
  //memset(&block->nodes[index.node_index], 0, sizeof(EditorNode));
  //block->currentOccupiedCount -= 1;
}

void RemoveNodeOutputConnections(EditorNode *node, Editor *editor){
  it(outputIndex, node->output_count){
    auto output_connection = node->output_connections[outputIndex];
    it(i, output_connection.count){
      auto connection = &output_connection[i];
      auto dest = GetNode(connection->node_index, editor);
      dest->inputConnections[connection->io_index].node_index = InvalidNodeIndex();
    }
  }
}

static inline void SimulateNode(EditorNode *node, Editor *editor);

static inline
void TransmitOutputAndSimulateConnectedNodes(EditorNode *node, uint32_t output_index, uint8_t outputState, Editor *editor){
  assert(outputState != NodeState_NONE);
  assert(output_index < node->output_count);
  node->signal_state = (NodeState)outputState;

  DynamicArray<NodeConnection>& outputConnections = node->output_connections[output_index];
  it(i, outputConnections.count){
    NodeConnection *connection = &outputConnections[i];
    auto connectedNode = GetNode(connection->node_index, editor);
    connectedNode->input_state[connection->io_index] = (NodeState)outputState;
    SimulateNode(connectedNode, editor);
  }
}

static inline
uint64_t GetNodeOutputState(const uint32_t type, const NodeState *inputState, const size_t inputCount, uint8_t *outputState){
  switch(type){

    //TODO(Torin) inputs should not be duplicated
    //simulating a input node is redundant see SimulateIC
    case NodeType_INPUT:{
      *outputState = inputState[0];
    };
    
    //TODO(Torin) Same thing ^^^^^^^^^^
    case NodeType_OUTPUT: {
      *outputState = (inputState[0] == NodeState_NONE) ? NodeState_LOW : inputState[0];
    }break;

    case NodeType_AND:{
      if(inputState[0] == NodeState_NONE) return 0;
      if(inputState[1] == NodeState_NONE) return 0;
      *outputState = inputState[0] & inputState[1];
    }break;

    case NodeType_OR:{
      if(inputState[0] == NodeState_NONE) return 0;
      if(inputState[1] == NodeState_NONE) return 0;
      *outputState = inputState[0] | inputState[1];
    }break;

    case NodeType_XOR:{
      if(inputState[0] == NodeState_NONE) return 0;
      if(inputState[1] == NodeState_NONE) return 0;
      *outputState = inputState[0] ^ inputState[1];
    }break;

    default:{
      assert(false);
    };
  }

  return 1;
}

static inline void SimulateICNode(ICNode *node, ICDefinition *icdef);


static inline
void TransmitICNodeOutputAndSimulateConnectedNodes(ICNode *node, size_t outputSlot, uint8_t signal, ICDefinition *icdef){
  for(size_t n = 0; n < node->connection_count_per_output[outputSlot]; n++){
    ICNodeConnection *connection = &node->output_connections[n]; 
    ICNode *connectedNode = &icdef->nodes[connection->node_index];
    connectedNode->input_state[connection->io_index] = (NodeState)signal;
    SimulateICNode(connectedNode, icdef);
  }
}

static inline
void SimulateICNode(ICNode *node, ICDefinition *icdef){
  switch(node->type){

    case NodeType_OUTPUT:{
      //NOTE(Torin) Intentionaly does nothing for now
      //Output is already stored in this nodes input_state
      //and is handled after the IC has been fully simulated
    }break;


    default: {
      if(node->type > NodeType_COUNT) assert(false);

      uint8_t outputState = 0;
      if(GetNodeOutputState(node->type, node->input_state, node->input_count, &outputState)){
        assert(node->output_count == 1);

        for(size_t n = 0; n < node->connection_count_per_output[0]; n++){
          ICNodeConnection *connection = &node->output_connections[n]; 
          ICNode *connectedNode = &icdef->nodes[connection->node_index];
          connectedNode->input_state[connection->io_index] = (NodeState)outputState;
          SimulateICNode(connectedNode, icdef);
        }
      }
    } break;
  }
}

static inline
void SimulateIC(ICDefinition *icdef, NodeState *inputs, NodeState *outputs){
  for(size_t i = 0; i < icdef->input_count; i++)
    if(inputs[i] == NodeState_NONE) return;

  //TODO(Torin) This should just emit outputs! 
  //NOTE(Torin) The first (input_count) nodes are inputs 
  for(size_t i = 0; i < icdef->input_count; i++){
    ICNode *node = &icdef->nodes[i];
    assert(node->type == NodeType_INPUT);
    TransmitICNodeOutputAndSimulateConnectedNodes(node, 0, inputs[i], icdef);
  }

  for(size_t i = 0; i < icdef->output_count; i++){
    const ICNode *ic_outputs = icdef->nodes + icdef->input_count;
    outputs[i] = ic_outputs[i].input_state[0];
  }
}

static inline
void SimulateNode(EditorNode *node, Editor *editor){
  switch(node->type){
    
    case NodeType_INPUT: {
      TransmitOutputAndSimulateConnectedNodes(node, 0, node->signal_state, editor); 
    }break;

    case NodeType_OUTPUT:{
      node->signal_state = (node->input_state[0] == NodeState_NONE) ? NodeState_LOW : node->input_state[0];
    }break;


    default:{

      if(node->type > NodeType_COUNT){
        for(size_t i = 0; i < node->input_count; i++)
          if(node->input_state[i] == NodeState_NONE) return;

        size_t ic_index = node->type - (NodeType_COUNT + 1);
        auto ic = editor->icdefs[ic_index];

        assert(node->input_count == ic->input_count);
        assert(node->output_count > 0);
        NodeState outputs[node->output_count];
        SimulateIC(ic, node->input_state, outputs);
        for(size_t i = 0; i < node->output_count; i++){
          TransmitOutputAndSimulateConnectedNodes(node, i, outputs[i], editor);
        }


      } else {
        uint8_t output_state;
        if(GetNodeOutputState(node->type, node->input_state, node->input_count, &output_state)){
          TransmitOutputAndSimulateConnectedNodes(node, 0, output_state, editor);
        }
      }
    }break;


  }
}


#if 0
static inline
void iterate_nodes(NodeBlock **blocks, size_t count, std::function<void(EditorNode*, NodeIndex)> procedure){
  for(size_t i = 0; i < count; i++){ 
    NodeBlock *block = blocks[i]; 
    for(size_t n = 0; n < NodeBlock::NODE_COUNT; n++){ 
      if(block->isOccupied[n]) {
        NodeIndex index = { (uint16_t)i, (uint16_t)n };
        procedure(&block->nodes[n], index);
      }
    }
  }
}
#endif


#if 0
static inline
void iterate_nodes(Editor *editor, std::function<void(EditorNode*)> procedure){
  NodeBlock **blocks = editor->nodeBlocks.data;
  size_t count = editor->nodeBlocks.count;
  for(size_t i = 0; i < count; i++){ 
    NodeBlock *block = blocks[i]; 
    for(size_t n = 0; n < NodeBlock::NODE_COUNT; n++){ 
      if(block->isOccupied[n]) {
        procedure(&block->nodes[n]);
      }
    }
  }
}
#endif

static inline
void iterate_nodes(Editor *editor, std::function<void(EditorNode*)> procedure){
  it(i, editor->nodes.count){
    auto node = editor->nodes[i];
    procedure(node);
  }
}

static inline
void iterate_nodes(Editor *editor, std::function<void(EditorNode*, NodeIndex)> procedure){
  it(i, editor->nodes.count){
    auto node = editor->nodes[i];
    NodeIndex index;
    index.node_index = i;
    index.node_ptr = node;
    procedure(node, index);
  }
}

//TODO(Torin) Why should this care about an editor ptr
//Just directly pass the node block array it signals intent better


static inline
void SimulationStep(Editor *editor){
  iterate_nodes(editor, [](EditorNode *node){
    memset(node->input_state, NodeState_NONE, node->input_count * sizeof(NodeState));
  });

  for(size_t ic_index = 0; ic_index < editor->icdefs.count; ic_index++){
    ICDefinition *icdef = editor->icdefs[ic_index];
    for(size_t i = 0; i < icdef->node_count; i++){
      ICNode *node = &icdef->nodes[i];
      for(size_t n = 0; n < node->input_count; n++){
        node->input_state[n] = NodeState_NONE;
      }
    }
  }
  

  for(size_t i = 0; i < editor->inputs.count; i++){
    EditorNode *node = editor->inputs[i];
    SimulateNode(node, editor);
  }
}

static inline
void DrawNodeDebugInfo(EditorNode *node){
  ImGui::Begin("NodeDebugInfo");
  ImGui::Text("type: %s", NodeName[node->type]);
  ImGui::Text("input_count: %zu", node->input_count);
  ImGui::Text("output_count: %zu", node->output_count);
  if(ImGui::CollapsingHeader("InputConnections")){
    it(i, node->input_count){
    }
  }

  if(node->output_count > 0){
    it(i, node->output_count){
      if(ImGui::TreeNode((void*)i, "output %zu", i)){
        auto &outputSlot = node->output_connections[i];
        it(n, outputSlot.count){
          
        }
        
        ImGui::TreePop();
      }
    }
  }

  ImGui::End();
}


void DrawEditor(Editor *editor){
  static const ImU32 GRID_COLOR = ImColor(200,200,200,40);
  static const float GRID_SIZE = 16.0f;
  static const float NODE_SLOT_RADIUS = 6.0f;
  static const ImVec2 NODE_WINDOW_PADDING(8.0f, 8.0f);

  static const ImColor CONNECTION_DEFAULT_COLOR = ImColor(150,50,50);
  static const ImColor CONNECTION_ACTIVE_COLOR = ImColor(220, 220, 110);

  static const ImColor NODE_BACKGROUND_SELECTED_COLOR = ImColor(90,90,90);
  static const ImColor NODE_BACKGROUND_DEFAULT_COLOR = ImColor(60,60,60);
  static const ImColor NODE_OUTLINE_COLOR = ImColor(100,100,100);

  static ImVec2 scrolling = ImVec2(0.0f, 0.0f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
  ImGui::SetNextWindowSize(ImVec2(1280, 720));
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  if(!ImGui::Begin("Example: Custom Node Graph", 0, flags)){
    ImGui::End();
    return;
  }

  // Draw a list of nodes on the left side
  bool open_context_menu = false;
  
  static bool is_context_menu_open = false;
  static NodeIndex node_hovered = InvalidNodeIndex();
  static NodeIndex dragNodeIndex = InvalidNodeIndex();

  static int dragSlotIndex = -1;

  if(is_context_menu_open == false){
    node_hovered = InvalidNodeIndex();
  }

  //Sidepanel
  ImGui::BeginChild("SidePanel", ImVec2(256, 0));
  DrawToolbarVerticaly(&editor->toolbar, editor);
  ImGui::EndChild();
  ImGui::SameLine();
  


  ImGui::BeginGroup();

  // 	 our child canvas
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
  ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60,60,70,200));
  ImGui::BeginChild("scrolling_region", ImVec2(0,0), true, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoMove);
  ImGui::PushItemWidth(120.0f);

  ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 offset = ImGui::GetCursorScreenPos() - scrolling;
  ImVec2 canvasMouseCoords = ImGui::GetMousePos() - canvasOrigin;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->ChannelsSplit(2);

  //@Draw @Grid
  ImVec2 win_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_sz = ImGui::GetWindowSize();
  for(float x = fmodf(offset.x,GRID_SIZE); x < canvas_sz.x; x += GRID_SIZE)
    draw_list->AddLine(ImVec2(x,0.0f)+win_pos, ImVec2(x,canvas_sz.y)+win_pos, GRID_COLOR);
  for(float y = fmodf(offset.y,GRID_SIZE); y < canvas_sz.y; y += GRID_SIZE)
    draw_list->AddLine(ImVec2(0.0f,y)+win_pos, ImVec2(canvas_sz.x,y)+win_pos, GRID_COLOR);

  // Display links
  draw_list->ChannelsSetCurrent(0); // Background


  auto GetNodeInputSlotPos = [](const EditorNode *node, const int slotIndex) -> ImVec2 {
    auto result = ImVec2(node->position.x, node->position.y + node->size.y * ((float)slotIndex+1) / ((float)2+1));
    return result;
  };

  auto GetNodeOutputSlotPos = [](const EditorNode *node, const int slotIndex) -> ImVec2 {
    auto result = ImVec2(node->position.x + node->size.x, node->position.y + node->size.y * ((float)slotIndex+1) / ((float)1+1));
    return result;
  };

  iterate_nodes(editor, [&](EditorNode *a, NodeIndex index){
    it(outputIndex, a->output_count){
      auto outputConnections = a->output_connections[outputIndex];
      it(n, outputConnections.count){
        ImVec2 p1 = offset + GetNodeOutputSlotPos(a, outputIndex);
        EditorNode *b = GetNode(outputConnections[n].node_index, editor);
        ImVec2 p2 = offset + GetNodeInputSlotPos(b, outputConnections[n].io_index);
        auto color = a->signal_state ? CONNECTION_ACTIVE_COLOR : CONNECTION_DEFAULT_COLOR;
        draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, color, 3.0f);
      }
    }
  });


  bool nodeWasClicked = false;
  

  iterate_nodes(editor, [&](EditorNode *node, NodeIndex index){
    ImVec2 node_rect_min = offset + node->position;
    ImVec2 node_rect_max = node_rect_min + node->size;

    ImGui::PushID(index.node_index);
    draw_list->ChannelsSetCurrent(1); // Foreground
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(node_rect_min + ImVec2(24, 24));
    switch(node->type){
      case NodeType_INPUT:{
        ImGui::SetCursorScreenPos(node_rect_min + ImVec2(4, 4));
        const char *text = node->signal_state ? "1" : "0";
        auto color = node->signal_state ? CONNECTION_ACTIVE_COLOR : CONNECTION_DEFAULT_COLOR;
        //ImGui::PushStyleColor(ImGuiCol_Button, color);

        if(ImGui::Button(text, ImVec2(32, 32))){
          if(node->signal_state == NodeState_LOW){
            node->signal_state = NodeState_HIGH;
          } else if (node->signal_state == NodeState_HIGH){
            node->signal_state = NodeState_LOW;
          } else {
            assert(false);
          }
        }
        //ImGui::PopStyleColor();
      }break;

      case NodeType_OUTPUT:{
        const char *text = node->signal_state ? "1" : "0";
        ImGui::Text(text);
      }break;

      default:{
        ImGui::Text(NodeName[node->type]);
      } break;
    }
    ImGui::EndGroup();

    // Display node box
    ImGui::SetCursorScreenPos(node_rect_min);

    ImGui::InvisibleButton("node", node->size);
    ImGui::PopID();
    
    if(ImGui::IsItemHovered()){
      node_hovered = index;
      if(ImGui::IsMouseClicked(1)) {
        open_context_menu = 1;
      } else if (ImGui::IsMouseClicked(0) && editor->mode != EditorMode_SelectBox){
        if(!ArrayContains(node_hovered, editor->selectedNodes)){
          editor->selectedNodes.count = 0;
          ArrayAdd(node_hovered, editor->selectedNodes);
        }
      }
    }

    bool node_moving_active = ImGui::IsItemActive();

    if(node_moving_active && ImGui::IsMouseDragging(0)){
      for(size_t i = 0; i < editor->selectedNodes.count; i++){
        EditorNode *selectedNode = GetNode(editor->selectedNodes[i], editor);
        selectedNode->position += ImGui::GetIO().MouseDelta;
      }
    }

    ImColor nodeColor = NODE_BACKGROUND_DEFAULT_COLOR;
    if(ArrayContains(index, editor->selectedNodes)){
      nodeColor = NODE_BACKGROUND_SELECTED_COLOR;
    }

    draw_list->ChannelsSetCurrent(0); // Background
    draw_list->AddRectFilled(node_rect_min, node_rect_max, nodeColor, 4.0f); 
    draw_list->AddRect(node_rect_min, node_rect_max, NODE_OUTLINE_COLOR, 4.0f);
  });
  draw_list->ChannelsMerge();

  //Draw and Update Node IO slots
  iterate_nodes(editor, [&](EditorNode *node, NodeIndex index){
    ImVec2 node_rect_min = offset + node->position - ImVec2(12,12);
    ImGui::SetCursorScreenPos(node_rect_min);
    ImVec2 size = ImVec2(node->size.x + 32, node->size.x + 32);
    ImGui::PushID(index.node_index);
    ImGui::InvisibleButton("##node", size);
    ImGui::PopID();

    bool hovered_slot_is_input = false;
    int hovered_slot_index = -1;

    //TODO(Torin) HACK imgui is not returning true on any of these when it should so were checking every single
    //node and its io slots for intersection on a mouse click
    if(ImGui::IsItemHovered() || ImGui::IsItemClicked() || ImGui::IsItemActive() || ImGui::IsMouseClicked(0)) {
      size_t total_slot_count = node->input_count + node->output_count;
      for(size_t slot_index = 0; slot_index < total_slot_count; slot_index++){
        bool is_input_slot = slot_index < node->input_count;
        const float NODE_SLOT_RADIUS_SQUARED = NODE_SLOT_RADIUS*NODE_SLOT_RADIUS;
        ImVec2 deltaSlot = is_input_slot ? GetNodeInputSlotPos(node, slot_index) : GetNodeOutputSlotPos(node, slot_index - node->input_count);
        deltaSlot -= scrolling;
        deltaSlot -= canvasMouseCoords; 
        deltaSlot.x = abs(deltaSlot.x);
        deltaSlot.y = abs(deltaSlot.y);

        if((deltaSlot.x*deltaSlot.x)+(deltaSlot.y*deltaSlot.y) < NODE_SLOT_RADIUS_SQUARED){
          if(is_input_slot){
            hovered_slot_index = slot_index;
            hovered_slot_is_input = true;
          } else {
            hovered_slot_index = slot_index - node->input_count;
            hovered_slot_is_input = false;
          }
          break;
        }
      }
    }

    static const ImColor default_color = ImColor(150,150,150,150);
    static const ImColor hover_color   = ImColor(220, 150, 150, 150);
    for(int slot_idx = 0; slot_idx < node->input_count; slot_idx++) {
      const ImColor color = ((hovered_slot_index == slot_idx) && (hovered_slot_is_input == true)) ? hover_color : default_color;
      draw_list->AddCircleFilled(offset + GetNodeInputSlotPos(node, slot_idx), NODE_SLOT_RADIUS, color);
    }

    for(int slot_idx = 0; slot_idx < node->output_count; slot_idx++){
      const ImColor color = ((hovered_slot_index == slot_idx) && (hovered_slot_is_input == false)) ? hover_color : default_color;
      draw_list->AddCircleFilled(offset + GetNodeOutputSlotPos(node, slot_idx), NODE_SLOT_RADIUS, color);
    }
      
    if(hovered_slot_index != -1){
      if(ImGui::IsMouseDoubleClicked(0)){
        RemoveNodeOutputConnections(GetNode(dragNodeIndex, editor), editor);
      } else if(ImGui::IsMouseClicked(0)){
        if(!IsValid(dragNodeIndex) && !hovered_slot_is_input){
          dragNodeIndex = index;
          dragSlotIndex = hovered_slot_index;
        } else if (IsValid(dragNodeIndex) && dragNodeIndex != index && hovered_slot_is_input){
          EditorNode *sourceNode = GetNode(dragNodeIndex, editor);
          EditorNode *destNode = node;

          if(!IsValid(destNode->inputConnections[hovered_slot_index].node_index)){
            //TODO(Torin) Insure that an output connection cannot have two connections to the same input
            //It appears that this is currently imposible already **BUT** only because single input sources
            //are allowed

            NodeConnection outputToInput = {};
            outputToInput.node_index = index;
            outputToInput.io_index = hovered_slot_index;
            auto &outputConnections = sourceNode->output_connections[dragSlotIndex];
            ArrayAdd(outputToInput, outputConnections);

            NodeConnection inputToOutput;
            inputToOutput.node_index = dragNodeIndex;
            inputToOutput.io_index = dragSlotIndex;
            destNode->inputConnections[hovered_slot_index] = inputToOutput;
            dragNodeIndex = InvalidNodeIndex();
            dragSlotIndex = 0;
          }
        }
      }
    }
  });

  //@Dragging 
  if(IsValid(dragNodeIndex)){
    EditorNode *sourceNode = GetNode(dragNodeIndex, editor);
    ImVec2 p1 = ImGui::GetMousePos();
    ImVec2 p2 = offset + GetNodeOutputSlotPos(sourceNode, dragSlotIndex);
    draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
  }

  if(ImGui::IsMouseClicked(1) && ImGui::IsMouseDown(1)){
    if(IsValid(dragNodeIndex)){
      dragNodeIndex = InvalidNodeIndex();
    } else {
      open_context_menu = 1;
    }
  }

  if(open_context_menu){
    ImGui::OpenPopup("context_menu");
    is_context_menu_open = true;
  }

  switch(editor->mode){
    case EditorMode_None:{
      if(ImGui::IsMouseDragging(0)){
        if(node_hovered != InvalidNodeIndex()) break;
        editor->selectBoxOrigin = ImGui::GetMousePos();
        editor->mode = EditorMode_SelectBox;
      }



    } break;

    case EditorMode_DEFAULT:{
      if(node_hovered == InvalidNodeIndex()){
        const ImColor color = ImColor(100, 100, 100, 100);
        draw_list->AddRectFilled(ImGui::GetMousePos(), ImGui::GetMousePos() + ImVec2(64, 64), color);

        if(ImGui::IsMouseClicked(0)){
        auto node = CreateNode(editor->placementNodeType, editor);
        node->position = ImGui::GetMousePos();
        }
      }


    } break;

    case EditorMode_SelectBox:{
      if(ImGui::IsMouseDragging(0) == 0){
        editor->selectedNodes.count = 0;
        auto end = ImGui::GetMousePos();
        Rectangle selectBoxBounds;
        selectBoxBounds.minX = Min(editor->selectBoxOrigin.x, end.x);
        selectBoxBounds.maxX = Max(editor->selectBoxOrigin.x, end.x);
        selectBoxBounds.minY = Min(editor->selectBoxOrigin.y, end.y);
        selectBoxBounds.maxY = Max(editor->selectBoxOrigin.y, end.y);
        iterate_nodes(editor, [&](EditorNode *node, NodeIndex index){
          Rectangle nodeRect = {node->position.x, node->position.y, node->position.x + node->size.x, node->position.y + node->size.y};
          if(Intersects(selectBoxBounds, nodeRect)){
            ArrayAdd(index, editor->selectedNodes);
          }
        });

        editor->mode = EditorMode_None;
      }
    } break;
  }

  if(editor->mode == EditorMode_SelectBox){
    ImColor color = ImColor(100, 100, 100, 100);
    draw_list->AddRectFilled(editor->selectBoxOrigin, ImGui::GetMousePos(), color);
  }

  // Draw context menu
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
  if(ImGui::BeginPopup("context_menu")){
    ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
    if(IsValid(node_hovered)){

      if(ImGui::MenuItem("Delete")){
        if(ArrayContains(node_hovered, editor->selectedNodes)){
          for(size_t i = 0; i < editor->selectedNodes.count; i++)
            DeleteNode(editor->selectedNodes[i], editor);
          editor->selectedNodes.count = 0;
        } else {
          DeleteNode(node_hovered, editor);
        }
      }

      
      if(ImGui::MenuItem("Create IC")){
        DynamicArray<EditorNode *> inputs;
        DynamicArray<EditorNode *> outputs;
        DynamicArray<EditorNode *> logic;

        it(i, editor->selectedNodes.count){
          EditorNode *node = GetNode(editor->selectedNodes[i], editor);
          if(node->type == NodeType_INPUT) {
            ArrayAdd(node, inputs);
          } else if (node->type == NodeType_OUTPUT){
            ArrayAdd(node, outputs);
          } else {
            ArrayAdd(node, logic);
          }
        }

        size_t required_memory = sizeof(ICDefinition);
        for(size_t i = 0; i < editor->selectedNodes.count; i++){
          EditorNode *node = GetNode(editor->selectedNodes[i], editor);
          required_memory += sizeof(ICNode);
          required_memory += node->input_count * sizeof(NodeState);
          required_memory += node->output_count * sizeof(uint32_t);
          required_memory += node->output_count * sizeof(ICNodeConnection);
        }

        ICDefinition *icdef = (ICDefinition *)calloc(required_memory, 1);
        ArrayAdd(icdef, editor->icdefs);
        icdef->input_count = inputs.count;
        icdef->output_count = outputs.count;
        icdef->node_count = editor->selectedNodes.count;

        MStack mstack = {};
        mstack.base = (uintptr_t)(icdef + 1);
        mstack.size = required_memory - sizeof(ICDefinition);
        icdef->nodes = MStackPushArray(ICNode, icdef->node_count, &mstack); 
        
        auto InitICNode = [](ICNode *ic_node, EditorNode *ed_node, MStack *mstack){
          ic_node->type = ed_node->type;
          ic_node->input_count = ed_node->input_count; 
          ic_node->output_count = ed_node->output_count;
          
          if(ic_node->input_count > 0) {
            ic_node->input_state = MStackPushArray(NodeState, ic_node->input_count, mstack);
          }          

          if(ic_node->output_count > 0){
            ic_node->connection_count_per_output = MStackPushArray(uint32_t, ic_node->output_count, mstack);
            size_t totalConnectionCount = 0;
            for(size_t n = 0; n < ic_node->output_count; n++){
              DynamicArray<NodeConnection>& connections = ed_node->output_connections[n];
              ic_node->connection_count_per_output[n] = connections.count;
              totalConnectionCount += connections.count;
            }
            ic_node->output_connections = MStackPushArray(ICNodeConnection, totalConnectionCount, mstack);
          }
        };
        
        //TODO(Torin) @UNSAFE This stack allocation could potentialy be enormous
        EditorNode *icToEdMap[editor->selectedNodes.count];
        size_t currentICNodeIndex = 0;
        for(size_t i = 0; i < inputs.count; i++){
          EditorNode *ed_node = inputs[i];
          ICNode *ic_node = &icdef->nodes[currentICNodeIndex];
          InitICNode(ic_node, ed_node, &mstack);
          icToEdMap[currentICNodeIndex] = ed_node;
          currentICNodeIndex++;
        }

        for(size_t i = 0; i < outputs.count; i++){
          EditorNode *ed_node = outputs[i];
          ICNode *ic_node = &icdef->nodes[currentICNodeIndex];
          InitICNode(ic_node, ed_node, &mstack);
          icToEdMap[currentICNodeIndex] = ed_node;
          currentICNodeIndex++;
        }

        //TODO(Torin) Sort by distance to inputs?
        for(size_t i = 0; i < logic.count; i++){
          EditorNode *ed_node = logic[i];
          ICNode *ic_node = &icdef->nodes[currentICNodeIndex];
          InitICNode(ic_node, ed_node, &mstack);
          icToEdMap[currentICNodeIndex] = ed_node;
          currentICNodeIndex++;
        }

        for(size_t i = 0; i < icdef->node_count; i++){
          ICNode *icnode = &icdef->nodes[i];
          EditorNode *ednode = icToEdMap[i];
          size_t currentConnectionIndex = 0;
          for(size_t n = 0; n < icnode->output_count; n++){
            DynamicArray<NodeConnection>& connections = ednode->output_connections[n]; 
            for(size_t j = 0; j < connections.count; j++){
              NodeConnection *edConnection = &connections[j];
              ICNodeConnection icConnection = {};
              icConnection.io_index = edConnection->io_index;

              EditorNode *connectedNodePtr = edConnection->node_index.node_ptr;
              size_t connectedNodeIndex = 0;
              if(!LinearSearch<EditorNode *>(connectedNodePtr, icToEdMap, icdef->node_count, &connectedNodeIndex)){
                assert(false);
              }
              icConnection.node_index = connectedNodeIndex;
              icnode->output_connections[currentConnectionIndex] = icConnection;
            }
          }
        }

        for(size_t i = 0; i < editor->selectedNodes.count; i++){
          DeleteNode(editor->selectedNodes[i], editor);
        }

        uint32_t node_type = NodeType_COUNT + 1 + (editor->icdefs.count - 1);
        auto node = CreateNode(node_type, editor);

        ArrayDestroy(inputs);
        ArrayDestroy(outputs);
        ArrayDestroy(logic);
      }
    }
    else {

      for(size_t i = 0; i < NodeType_COUNT; i++){
        if(ImGui::MenuItem(NodeName[i])) {
          auto node = CreateNode((NodeType)i, editor);
          node->position = canvasMouseCoords;
        }
      }

      ImGui::Separator();

      it(i, editor->icdefs.count){
        if(ImGui::MenuItem("IC XXX")){
          auto node = CreateNode(i + NodeType_COUNT + 1, editor);
        }
      }


    }
    ImGui::EndPopup();
  } else {
    is_context_menu_open = false;
  }
  ImGui::PopStyleVar();

  // Scrolling
  if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
      scrolling = scrolling - ImGui::GetIO().MouseDelta;

  ImGui::PopItemWidth();
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  ImGui::EndGroup();

  ImGui::End();

#if 0
  if(editor->selectedNodes.count == 1){
    DrawNodeDebugInfo(GetNode(editor->selectedNodes[0], editor));
  }
#endif
}

int main(){
  QuickAppStart("Hardware Simulator", 1280, 720);
  Editor editor = {};
  editor.toolbar.nodeTypes[0] = NodeType_INPUT;
  editor.toolbar.nodeTypes[1] = NodeType_OUTPUT;
  editor.toolbar.count = 10;

  QuickAppLoop([&]() {
    SimulationStep(&editor);
    DrawEditor(&editor);
  });
}