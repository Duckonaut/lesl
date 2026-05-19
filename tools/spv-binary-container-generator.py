import json
import os
import sys

class SPIRVBinaryContainerGenerator:
    def __init__(self, grammar):
        self.grammar = grammar
        self.instructions = self.grammar['instructions']

    def labelize(self, name):
        label = ""
        last_was_lower = False
        for c in name:
            if c == ' ':
                label += "_"
                last_was_lower = False
                continue
            if not c.isalnum():
                continue
            if (c.isupper() and last_was_lower):
                label += "_"
            label += c.lower()
            last_was_lower = c.islower()

        if label == "default": # default is a reserved keyword, and is used in the grammar
            label += "_"
        return label

    def opname(self, full_name):
        return full_name[2:]

    def generate_op_method(self, instruction, output):
        has_optargs = 0
        has_result = -1
        operands_raw = instruction['operands'] if 'operands' in instruction else []
        operands = []
        for operand in operands_raw:
            kind = operand['kind']
            ty = 'uint32_t'
            if kind == 'LiteralString':
                ty = 'const char*'
            if kind == 'IdResult':
                has_result = len(operands)

            vararg = False
            optarg = False
            if 'quantifier' in operand:
                if operand['quantifier'] == '*':
                    vararg = True
                if operand['quantifier'] == '?':
                    optarg = True
                    has_optargs += 1

            name = self.labelize(kind)
            if 'name' in operand and not vararg:
                name = self.labelize(operand['name'])

            operands.append({
                'kind': kind,
                'ty': ty,
                'vararg': vararg,
                'optarg': optarg,
                'name': name
            })

        for i in range(has_optargs + 1):
            partial = operands[:len(operands)-i]

            self.generate_method_overload(instruction, partial, -1, output)

            if has_result != -1:
                partial = operands[:len(operands)-i]
                partial.pop(has_result)
                self.generate_method_overload(instruction, partial, has_result, output)

    def generate_method_overload(self, instruction, operands, create_result, output):
        has_varargs = False
        has_decoration = False
        has_string = -1

        for i, operand in enumerate(operands):
            if operand['vararg']:
                has_varargs = True
            if operand['kind'] == 'Decoration':
                has_decoration = True
            if operand['kind'] == 'LiteralString':
                has_string = i

        if create_result != -1:
            output.write("    uint32_t " + self.opname(instruction['opname']) + "New(")
        else:
            output.write("    void " + self.opname(instruction['opname']) + "(")
        for i, operand in enumerate(operands):
            if i > 0:
                output.write(", ")
            if operand['vararg']:
                output.write(operand['ty'] + "* operands, uint32_t count")
            elif operand['kind'] == 'Decoration':
                output.write("spv::Decoration decoration, uint32_t* parameters, uint32_t parameter_count")
            else:
                output.write(operand['ty'] + " " + operand['name'])


        output.write(") {\n")
        output.write("        uint32_t op = " + str(instruction['opcode']) + ";\n")
        operand_count_base = 1 + len(operands)
        if has_varargs:
            operand_count_base -= 1
        if has_string != -1:
            operand_count_base -= 1
        if create_result != -1:
            operand_count_base += 1
        output.write("        uint32_t operand_count = " + str(operand_count_base) + ";\n")
        if has_varargs:
            output.write("        operand_count += count;\n")
        if has_string != -1:
            output.write("        operand_count += 1 + (uint32_t)strlen(" + operands[has_string]['name'] + ") / 4;\n")
        if has_decoration:
            output.write("        operand_count += parameter_count;\n")
        output.write("        op |= operand_count << 16;\n")
        output.write("        push(op);\n")
        if create_result != -1:
            output.write("        uint32_t result_id = get_id();\n")
        for i, operand in enumerate(operands):
            if create_result == i:
                output.write("        push(result_id);\n")

            if operand['vararg']:
                output.write("        for (uint32_t i = 0; i < count; i++) {\n")
                output.write("            push(operands[i]);\n")
                output.write("        }\n")
            elif operand['ty'] == 'const char*':
                output.write("        uint32_t len = (uint32_t)strlen(" + operand['name'] + ");\n")
                output.write("        uint32_t word_len = 1 + len / 4;\n")
                output.write("        for (uint32_t i = 0; i < word_len; i++) {\n")
                output.write("            uint32_t word = 0;\n")
                output.write("            for (uint32_t j = 0; j < 4; j++) {\n")
                output.write("                uint8_t b = i * 4 + j < len ? " + operand['name'] + "[i * 4 + j] : 0;\n")
                output.write("                word |= b << (j * 8);\n")
                output.write("            }\n")
                output.write("            push(word);\n")
                output.write("        }\n")
            elif operand['kind'] == 'Decoration':
                output.write("        push(decoration);\n")
                output.write("        for (uint32_t i = 0; i < parameter_count; i++) {\n")
                output.write("            push(parameters[i]);\n")
                output.write("        }\n")
            else:
                output.write("        push(" + operand['name'] + ");\n")
        if create_result != -1:
            if create_result == len(operands):
                output.write("        push(result_id);\n")
            output.write("        return result_id;\n")
        output.write("    }\n\n")

    def generate(self, output):
        output.write("// This file is generated by spv-binary-container-generator.py\n")
        output.write("#pragma once\n\n")
        output.write("#ifndef spirv_HPP\n")
        output.write("#error \"This file must be included after the SPIRV-Headers spirv.hpp\"\n")
        output.write("#endif\n\n")
        output.write("#include <cstdint>\n")
        output.write("#include <cstring>\n")
        output.write("#include <vector>\n\n")
        output.write("namespace spvbc {\n\n")

        output.write("class BinaryContainer {\n")
        output.write("public:\n")
        output.write("    std::vector<uint32_t> words;\n")
        output.write("    uint32_t id_bound = 1;\n")
        output.write("    BinaryContainer() {\n")
        output.write("        words.reserve(1024);\n")
        output.write("        words.push_back(spv::MagicNumber);\n")
        output.write("        words.push_back(LESL_SPIRV_VERSION);\n")
        output.write("        words.push_back(0);\n") # Generator
        output.write("        words.push_back(0);\n") # Bound
        output.write("        words.push_back(0);\n") # Schema
        output.write("    }\n")

        output.write("    uint32_t* data() { return words.data(); }\n")
        output.write("    size_t size() { return words.size(); }\n")
        output.write("    void clear() { words.clear(); }\n")
        output.write("    void insert(std::vector<uint32_t> new_words, uint32_t start) {\n")
        output.write("        std::vector<uint32_t> carry;\n")
        output.write("        for (size_t i = start; i < words.size(); i++) {\n")
        output.write("            carry.push_back(words[i]);\n")
        output.write("        }\n")
        output.write("        words.resize(start);\n")
        output.write("        for (size_t i = 0; i < new_words.size(); i++) {\n")
        output.write("            words.push_back(new_words[i]);\n")
        output.write("        }\n")
        output.write("        for (size_t i = 0; i < carry.size(); i++) {\n")
        output.write("            words.push_back(carry[i]);\n")
        output.write("        }\n")
        output.write("    }\n")
        output.write("    uint32_t get_id() { return id_bound++; }\n")
        output.write("    void push(uint32_t word) { words.push_back(word); }\n")
        output.write("    void update_bound() { words[3] = id_bound; }\n")
        for instruction in self.instructions:
            self.generate_op_method(instruction, output)

        output.write("};\n\n")

        output.write("} // namespace spvbc\n")


if __name__ == '__main__':
    # Parse command line arguments
    if len(sys.argv) != 3:
        print("Usage: python spv-binary-container-generator.py <SPIR-V Grammar JSON> <Output Header>")
        sys.exit(1)

    input_spirv_json = sys.argv[1]
    output_header = sys.argv[2]

    grammar = json.load(open(input_spirv_json, 'r'))

    generator = SPIRVBinaryContainerGenerator(grammar)

    output = open(output_header, 'w')
    generator.generate(output)

