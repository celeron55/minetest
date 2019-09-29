import sys
import re
import copy

ENABLE_LOG = False
IN_PLACE = False
# Not actually needed because indent level is autodetected for each block
NO_NAMESPACE_INDENT = False
ANNOTATE_FILES = False

ANNOTATION_PREFIX = "STYLEFIX: "
ANNOTATION_POSTFIX = ""

def log(message):
	if ENABLE_LOG:
		print("-!- "+message)

input_filenames = []
for a in sys.argv[1:]:
	if a == '-v':
		ENABLE_LOG = True
	elif a == '-i':
		IN_PLACE = True
	elif a == '-b':
		NO_NAMESPACE_INDENT = True
	elif a == '-a':
		ANNOTATE_FILES = True
	else:
		input_filenames.append(a)

def create_zero_levels():
	return {
		"{}": 0,
		"[]": 0,
		"()": 0,
		"''": 0,
		'""': 0,
		"<>": 0,
		"=;": 0,
		"/**/": 0,
		"//": 0,
		"#": 0,
		"):{": 0,
		";": 0,
		# if(), for() and whatever without a block. Resets at end of statement
		# and at beginning of actual block; set by caller.
		"implicit_block": 0,
	}

class ParenMatch:
	def __init__(self):
		self._level = create_zero_levels()
		self.level_before_line = None
		self.level_before_line_end = None
		self.level_after_line = None
		self.level_lowest_on_line = None
		self.level_highest_on_line = None
		self.assignment_begin_paren_level = 0

	def feed_part(self, line, i):
		if self._level["/**/"] > 0:
			if line[i:i+2] == '*/' and line[i-1] != '/':
				self._level["/**/"] = 0
				return i + 2
			return i + 1  # Ignore all other comment content
		if line[i] == '\n':
			if self._level["//"] > 0:
				self._level["//"] = 0
			if self._level["#"] > 0 and line[i-1] != '\\':
				self._level["#"] = 0
			return i + 1  # Ignore line end otherwise
		if self._level["//"] > 0:
			return i + 1 # Ignore all comment content
		if self._level["''"] > 0:
			if line[i] == "\\":
				return i + 2
			if line[i] == "'":
				self._level["''"] = 0
			return i + 1  # Ignore '' content
		if self._level['""'] > 0:
			if line[i] == "\\":
				return i + 2
			if line[i] == '"':
				self._level['""'] = 0
			return i + 1  # Ignore "" content
		if line[i] == '/':
			if line[i+1] == '*':
				self._level["/**/"] = 1
				return i + 1
			if line[i+1] == '/':
				self._level["//"] = 1
				return i + 1
		if line[i] == '#':
			self._level["#"] = 1
			return i + 1
		if self._level["#"]:
			return i + 1 # Ignore macro content

		# Statements and implicit blocks
		if self._level[";"] == 0:
			if line[i] not in " \t\n()[]{};":
				#print(repr(line[i])+" begins statement")
				self._level[";"] = 1 # Automatically start a new statement
		if self._level[";"] > 0:
			if line[i] in ";{}":
				self._level[";"] = 0
				self._level["implicit_block"] = 0
			if line[i] == ':' and line[i-1] != ':':  # Not very precise
				self._level[";"] = 0
				self._level["implicit_block"] = 0
		if line[i] == '{':
			self._level["implicit_block"] = 0

		if line[i] == '=':
			if line[i+1] not in '=<>!' and line[i-1] not in '=<>!':
				self._level["=;"] = 1
				self.assignment_begin_paren_level = self._level["()"]
				return i + 1
		if line[i] == ';':
			self._level["<>"] = 0
			if self._level["=;"] > 0:
				self._level["=;"] = 0
				return i + 1
		if self._level["):{"] > 0:
			if line[i] == '{':
				self._level["):{"] = 0
				# Allow other processing
		if line[i] == ':' and line[i-1] == ')':
			self._level["):{"] = 1
			return i + 1
		if line[i] == '<':
			if line[i-1] != '<' and line[i+1] != '<':
				self._level["<>"] += 1
		if line[i] == '>':
			self._level["<>"] -= 1
		if line[i] in ["|", "&", "(", ")", "]", "["]:
			self._level["<>"] = 0
		for k in ["{}", "[]", "()", "''", '""']:
			if line[i] == k[0]:
				self._level[k] += 1
				return i + 1
			if line[i] == k[1]:
				self._level[k] -= 1
				if self._level[k] < 0:
					log("WARNING: resetting negative "+k+" level")
					self._level[k] = 0
				if k == "()" and self.assignment_begin_paren_level > self._level[k]:
					self._level["=;"] = 0
				return i + 1
		return i + 1

	def record_levels(self):
		for bracetype, level in self._level.items():
			if level < self.level_lowest_on_line[bracetype]:
				self.level_lowest_on_line[bracetype] = level
			if level > self.level_highest_on_line[bracetype]:
				self.level_highest_on_line[bracetype] = level

	def feed_line(self, line):
		self.level_lowest_on_line = copy.copy(self._level)
		self.level_highest_on_line = copy.copy(self._level)
		self.level_before_line = copy.copy(self._level)
		i = 0
		while True:
			# Record state just before the line ends
			if line[i] == '\n':
				self.level_before_line_end = copy.copy(self._level)
			# Process current position
			i = self.feed_part(line, i)
			# Record levels
			self.record_levels()
			# Stop if at end of line
			if i == len(line):
				break
			# Sanity check
			if i > len(line):
				print("Issue in feed_part()")
		self.level_after_line = copy.copy(self._level)

class DetectedBlock:
	def __init__(self, line_i, start_level, base_indent_level,
			base_levels, block_type=None):
		# If a block starts at the end of some line, the indentation of the
		# block should always be one tab level, not more like uncrustify makes
		# it in case the block belongs to a function parameter
		self.start_line = line_i
		self.open_level = start_level
		self.base_indent_level = base_indent_level
		if block_type == "namespace" and NO_NAMESPACE_INDENT:
			self.inner_indent_level = base_indent_level
		else:
			self.inner_indent_level = base_indent_level + 4
		self.base_levels = base_levels  # Basae ParenMatch levels inside block
		self.block_type = block_type    # None/"namespace"/something

BLOCK_TYPE_REGEXES = [
	("namespace", r'^[\t ]*namespace.*$'),
	("struct",    r'^[\t ]*struct.*$'),
	("class",     r'^[\t ]*class.*$'),
	("if",        r'^[\t ]*if.*$'),
	("for",       r'^[\t ]*for.*$'),
	("while",     r'^[\t ]*while.*$'),
	("lambda",    r'^.*\)\[.*$'),
	("enum",      r'^[\t ]*enum.*$'),
	("=",         r'^.*[^;=!<>]=[^;=!<>]*$'),  # == is probably in if(.*==.*){
]
STRUCTURE_BLOCK_TYPES = ["namespace", "struct"]
CODE_BLOCK_TYPES = [None, "if", "for", "while", "lambda"]
VALUE_BLOCK_TYPES = ["enum", "="]

class State:
	def __init__(self):
		self.match = ParenMatch()
		self.blocks = [] # Stack
		self.paren_level_indentations = {}  # Level -> starting indentation level
		self.paren_level_start_lines = {}  # Level -> starting line
		self.paren_level_identifiers = {}  # Level -> identifier
		self.next_block_type = None
		self.comment_orig_base_indent = None

		# Output values
		self.reset_output()

	def reset_output(self):
		# Annotations
		self.fix_annotation = None
		self.annotation_is_inside_macro = False  # Need for macro continuation
		self.annotation_is_inside_comment = False  # Need for avoiding /*/**/*/

		# Actual output
		self.indent_level = 0

	def print_debug_state(self, level_before, level_after):
		if self.blocks:
			log("base_levels : "+repr(self.blocks[-1].base_levels))
		log("level_before: "+repr(level_before))
		log("level_after : "+repr(level_after))

	def add_fix_annotation(self, description):
		log(description)
		if self.fix_annotation is not None:
			self.fix_annotation += "; "
		else:
			self.fix_annotation = ""
		self.fix_annotation += description

	def add_indent(self, d, description):
		if d == 0:
			return
		self.add_fix_annotation("indent"+("+="+str(d) if d >= 0
				else "-="+str(-d))+": "+description)
		self.indent_level += d

	def get_top_block_base_levels(self):
		if not self.blocks:
			return create_zero_levels()
		top_block = self.blocks[-1]
		return top_block.base_levels

	def feed_line(self, line, line_i):
		self.reset_output()

		if line.strip() == "":
			return

		self.match.feed_line(line)
		level_before = copy.copy(self.match.level_before_line)
		level_at_end = copy.copy(self.match.level_before_line_end)
		level_after = copy.copy(self.match.level_after_line)
		level_lowest = copy.copy(self.match.level_lowest_on_line)
		level_highest = copy.copy(self.match.level_highest_on_line)

		#self.add_fix_annotation("Levels before line: "+repr(level_before))

		# Measure original indentation level
		orig_indent_level = 0
		for c in line:
			if c == "\t":
				orig_indent_level += 4
			elif c == " ":
				orig_indent_level += 1
			else:
				break

		line_is_comment = False
		#if level_at_end["//"] > 0: # Bad; we care if the whole line is a comment
		if re.match(r'[\t ]*//.*', line):
			#self.add_fix_annotation("Line is C++ comment")
			line_is_comment = True
		if (re.match(r'[\t ]*/\*.*', line) or re.match(r'.*\*/', line) or
				level_after['/**/'] > 0):
			#self.add_fix_annotation("Line is C comment")
			line_is_comment = True
		if level_before['/**/'] > 0:
			#self.add_fix_annotation("Line is C comment continuation")
			line_is_comment = True
			self.annotation_is_inside_comment = True

		if not line_is_comment:
			self.comment_orig_base_indent = None

		line_is_macro = (level_at_end['#'] > 0)
		macro_continued = (level_before['#'] > 0)

		if line_is_macro:
			# Leave macros as-is
			self.indent_level = orig_indent_level
			if not macro_continued:
				self.add_fix_annotation("Line is macro")
			else:
				self.add_fix_annotation("Line is macro continuation")
				self.annotation_is_inside_macro = True
			return

		# Set indent level based on block level
		self.indent_level = 0
		if self.blocks:
			top_block = self.blocks[-1]
			self.indent_level = top_block.inner_indent_level

		# So let's process the REAL CODE
		if line_is_comment:
			# Fluctuate comment's indentation from block-based self.indent_level
			# by the amount the indentation originally fluctuates
			if self.comment_orig_base_indent is None:
				self.comment_orig_base_indent = orig_indent_level
			# Add difference to output level
			d = orig_indent_level - self.comment_orig_base_indent
			self.add_indent(d, "Comment's internal indentation variation")
			return

		# Detect parenthesis starting levels
		if level_highest["()"] > level_before["()"]:
			for paren_level in range(level_before["()"], level_highest["()"]):
				# NOTE: This isn't accurate because the line can contain
				#       multiple opening parenthesis with all having a different
				#       keyword
				identifier = None
				m = re.match(r'^.*?([^(){}\t ]+)[ \t]*\(.*$', line)
				if m is not None:
					identifier = m.group(1)
				#self.add_fix_annotation("identifier="+repr(identifier))
				self.paren_level_indentations[paren_level] = self.indent_level
				self.paren_level_start_lines[paren_level] = line_i
				self.paren_level_identifiers[paren_level] = identifier
				log("Detected paren level "+str(paren_level)+": indentation "+
						str(self.indent_level)+", start line "+str(line_i)+
						", identifier "+repr(identifier))

		# Get current block type
		current_block_type = None
		if self.blocks:
			block = self.blocks[-1]
			current_block_type = block.block_type
		is_value_block = (current_block_type in ["enum", "="])

		#
		# Block type
		#
		# Not ideal but generally works

		# Reset block type in these (rather common) cases
		if (level_lowest["{}"] < level_before["{}"] or
				level_lowest["()"] != level_highest["()"] or
				level_lowest["):{"] != level_highest["):{"]):
			self.next_block_type = None

		for (t, regex) in BLOCK_TYPE_REGEXES:
			if re.match(regex, line):
				self.next_block_type = t
				break
		# The '=' block type is inherited when there is no other option
		if self.next_block_type is None and current_block_type == '=':
			self.next_block_type = current_block_type

		#self.add_fix_annotation("Current block type: "+repr(current_block_type))
		#self.add_fix_annotation("Next block type: "+repr(self.next_block_type))

		#
		# Block level
		#

		# Update current block level
		while self.blocks:
			block = self.blocks[-1]
			if level_lowest["{}"] <= block.open_level:
				base_indent_level = block.base_indent_level
				self.add_fix_annotation("Block level "+str(block.open_level)+
						" end (begun on line "+str(block.start_line)+
						", base_indent_level="+str(base_indent_level)+")")
				self.blocks = self.blocks[:-1]
				# Fix indentation of last line if it begins with '}'
				if re.match(r'^[\t ]*}.*$', line):
					self.print_debug_state(level_before, level_after)
					self.add_indent(-4, "Fixing indent of block end")
			else:
				break

		# Block level detection: Detect block starts
		# Really works only if there is only one { on the line
		if level_after["{}"] > level_lowest["{}"]:
			block_open_level = level_lowest["{}"]
			base_indent = self.indent_level
			# If this line closes parenthesis, take the indent level from the
			# parentheesis starting indentation level
			use_paren_based_indentation = False
			if level_after["()"] < level_before["()"]:
				base_paren_level = level_after["()"]
				base_indent = self.paren_level_indentations[
						base_paren_level]
				use_paren_based_indentation = True
				self.add_fix_annotation("This line closes parenthesis; taking "+
						"indent level from the line that started the "+
						"parenthesis")
			if not use_paren_based_indentation and self.blocks:
				# Use the indent level of the outside block if possible
				parent = self.blocks[-1]
				if parent.inner_indent_level is not None:
					base_indent = parent.inner_indent_level
					self.add_fix_annotation("Basing on inner indentation level "+
							str(base_indent)+" of outside block level "+
							str(parent.open_level))
			block_type = self.next_block_type
			self.next_block_type = None
			self.add_fix_annotation("Block level "+
					str(block_open_level)+" begin; base indent "+
					str(base_indent)+", type "+repr(block_type))
			self.blocks.append(DetectedBlock(line_i, block_open_level,
					base_indent, level_after, block_type))
			if not use_paren_based_indentation:
				# Fix { to be on the correct indentation level
				d = base_indent - self.indent_level
				self.add_indent(d, "Fixing { to have correct indentation")

		#
		# Indentation level fine-tuning
		#

		# Indent some stuff
		block_base_levels = self.get_top_block_base_levels()

		# Label
		if re.match(r'^[\t ]*[a-zA-Z0-9_]*:$', line):
			self.add_indent(-4, "Label")

		# case
		if re.match(r'^[\t ]*case .*:$', line):
			self.add_indent(-4, "case")

		# Detect statements that look enough like function calls to be
		# considered statements (macro calls without trailing ';')
		# (if(), for() and others look like this too)
		# It can look like a statement only if the parenthesis are ending to the
		# block's base parenthesis level
		may_create_implicit_block = False
		if (level_highest["()"] > level_after["()"] and
				level_after["()"] == block_base_levels["()"] and
				level_after["[]"] == block_base_levels["[]"] and
				level_highest[";"] > 0 and
				level_after[";"] == level_highest[";"]):
			try:
				identifier = self.paren_level_identifiers[level_after["()"]]
				if identifier in ["if", "for", "while"]:
					#self.add_fix_annotation("Keyword "+repr(identifier))
					if level_lowest["{}"] == level_after["{}"]:
						self.add_fix_annotation("May create implicit "+
								repr(identifier)+" block")
						self.match._level["implicit_block"] = 1
						may_create_implicit_block = True
						# Cheat the state
						self.match._level[";"] = 0
				elif identifier and re.match(r'^[a-zA-Z0-9_]*$', identifier):
					if re.match(r'^.*\),?$', line):
						self.add_fix_annotation("Isn't a full statement but looks "+
								"like a function call to "+repr(identifier))
						# Cheat the state
						self.match._level[";"] = 0
					else:
						self.add_fix_annotation("Assuming multi-line statement")
				else:
					self.add_fix_annotation("Identifier "+repr(identifier)+
							" doesn't make this look like a function call")
			except KeyError:
				pass

		# Handle else's implicit block
		if re.match(r'^[ \t]*else[ \t]*$', line):
			self.add_fix_annotation("May create implicit "+
					repr("else")+" block")
			self.match._level["implicit_block"] = 1
			may_create_implicit_block = True
			# Cheat the state
			self.match._level[";"] = 0

		# Keep member initializers at proper indentation level
		if (current_block_type in ["struct", "class"] and
				level_before["):{"] > 0 and
				level_after["()"] <= block_base_levels["()"]):
			self.add_fix_annotation("Keeping member initializer at proper "+
					"indentation level")
			# Cheat the state
			self.match._level[";"] = 0

		# Implicit block indentation
		if (not may_create_implicit_block and # Handle blockless nested loop
				level_before["implicit_block"] > 0 and line.strip() != "{"):
			self.add_indent(4, "Implicit block")

		# Multi-line statements
		# NOTE: Applies also to if, for and such parameters
		if (
			not is_value_block and
			level_before[";"] > 0 and
			(
				level_lowest["{}"] == level_after["{}"] or
				level_before["()"] > block_base_levels["()"]
			) and
			line.strip() not in ["{}", "{", "}", "};", ")", ");", "]", "];"]
		):
			self.add_indent(8, "Multi-line statement")

		# Class member initializer indentation
		if (level_before["):{"] > 0 and
				line.strip() not in ["{}", "{", "}", "};"]):
			self.add_indent(4, "Adding indentation between ): and {")

		# Add two levels to inside multiline <> content because
		# uncrustify does not do that.
		if level_before["<>"] > block_base_levels["<>"]:
			self.add_indent(4, "Adding indentation to regular multiline <>")


def fix_line(line, state):
	# Remove all indentation
	num_whitespace_chars = 0
	for i in range(0, len(line)):
		num_whitespace_chars = i
		if line[i] not in ' \t':
			break
	line = line[num_whitespace_chars:]
	# Set wanted indentation
	tabs = int(state.indent_level / 4)
	remaining_spaces = state.indent_level - tabs * 4
	for i in range(0, remaining_spaces):
		line = " " + line
	for i in range(0, tabs):
		line = "\t" + line
	return line

for input_filename in input_filenames:
	f = open(input_filename)
	lines = f.readlines()
	f.close()
	state = State()
	fixed_lines = []
	for line_i, orig_line in enumerate(lines):
		if ANNOTATION_PREFIX in orig_line:
			continue
		state.feed_line(orig_line, line_i)
		fixed_line = fix_line(orig_line, state)
		if state.fix_annotation and ANNOTATE_FILES:
			if state.annotation_is_inside_comment:
				pre = "("
				post = ")"
			elif state.annotation_is_inside_macro:
				pre = "/* "
				post = " */ \\"
			else:
				pre = "// "
				post = ""
			annotation_line = (pre + ANNOTATION_PREFIX + state.fix_annotation +
					ANNOTATION_POSTFIX + post + "\n")
			if IN_PLACE:
				fixed_lines.append(annotation_line)
			else:
				sys.stdout.write(annotation_line)
		if ENABLE_LOG:
			sys.stdout.write("original "+str(line_i)+": "+orig_line)
			if fixed_line != orig_line:
				sys.stdout.write("   fixed "+str(line_i)+": "+fixed_line)
		else:
			if not IN_PLACE:
				sys.stdout.write(fixed_line)
		fixed_lines.append(fixed_line)
	if IN_PLACE:
		f.close()
		f = open(input_filename, "w")
		for line in fixed_lines:
			f.write(line)

# vim: set noet ts=4 sw=4:
