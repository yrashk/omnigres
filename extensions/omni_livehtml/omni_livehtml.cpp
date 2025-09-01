#include <cppgres.hpp>
#include <map>

extern "C" {
  PG_MODULE_MAGIC;

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

}

#include <span>

#include <zpp_bits.h>

namespace f = cppgres::fmt;

template <auto D> struct void_deleter {
  auto operator()(auto d) { D(d); }
};

struct str {
  str(lxb_html_document_t *doc, lexbor_str_t v) : doc_(doc), value(v) {}
  str(lxb_html_document_t *doc) : str(doc, {nullptr, 0}) {}

  ~str() { lexbor_str_destroy(&value, lxb_dom_interface_document(doc_)->text, false); }

  std::size_t length() { return value.length; }

  operator lexbor_str_t *() { return &value; }

private:
  lxb_html_document_t *doc_;
  lexbor_str_t value;
};

template <typename T, const char *type_name> struct node {
  static constexpr const char *node_type_name() { return type_name; }

  node(lxb_dom_node_t *node, lxb_html_document_t *doc) : node_(node), doc_(doc) {}
  node(lxb_dom_node_t *node)
      : node_(node), doc_(lxb_html_interface_document(node->owner_document)) {}
  node(std::span<const std::byte> buffer)
      : node(buffer,
             /* TODO: prevent resource leakage (document) */
             lxb_html_document_create()) {
    lxb_dom_node_insert_child(lxb_dom_interface_node(doc_), node_);
  }
  node(std::span<const std::byte> buffer, lxb_html_document_t *document) : doc_(document) {
    auto *element = lxb_dom_document_create_element(
        &document->dom_document, reinterpret_cast<const lxb_char_t *>("<template>"),
        sizeof("<template>"), nullptr);

    auto node = lxb_html_document_parse_fragment(
        document, element, reinterpret_cast<const unsigned char *>(buffer.data()), buffer.size());
    node_ = lxb_dom_node_first_child(node) ? lxb_dom_node_first_child(node) : node;
  }
  node(const char *buffer) : node(std::as_bytes(std::span(buffer, std::strlen(buffer)))) {}

  std::string to_string() {
    lexbor_str_t str{nullptr};
    lxb_html_serialize_tree_str(node_, &str);

    std::string string(reinterpret_cast<char *>(lexbor_str_data(&str)));
    lexbor_str_destroy(&str, node_->owner_document->text, false);

    return string;
  }

  std::vector<std::int64_t> path() {
    std::vector<std::int64_t> result;

    lxb_dom_node_t *current = node_;
    while (current->parent && current->parent->type != LXB_DOM_NODE_TYPE_DOCUMENT) {
      uint32_t index = 0;
      for (auto *sibling = current->parent->first_child; sibling != current;
           sibling = sibling->next) {
        index++;
      }
      result.insert(result.begin(), index);
      current = current->parent;
    }

    return result;
  }

  std::string to_string_path() {
    auto path_ = path();
    std::string result = "/";
    for (size_t i = 0; i < path_.size(); ++i) {
      result += std::to_string(path_[i]);
      if (i < path_.size() - 1)
        result += "/";
    }
    return result;
  }

  operator lxb_html_document_t *() { return doc_; }
  operator lxb_dom_node_t *() { return node_; }

  T parent() { return T(lxb_dom_node_parent(node_)); }
  void append_child(T &node) { lxb_dom_node_insert_child(node_, node); }

  void remove() { lxb_dom_node_remove(node_); }
  void insert_after(T &new_node) { lxb_dom_node_insert_after(node_, new_node); }

  template <typename T_> requires std::derived_from<T_, node<T_, T_::node_type_name()>>
  T_ replace(T_ &new_node) {
    T_ new_node_ = T_(new_node);
    auto node = new_node.operator lxb_dom_node_t *();
    if (lxb_html_interface_document(node->owner_document) !=
        (lxb_html_interface_document(node_->owner_document))) {
      auto s = new_node.to_string();
      std::span<const char> cs(s.c_str(), s.size());
      new_node_ = T_(std::as_bytes(cs), lxb_html_interface_document(node_->owner_document));
    }
    lxb_dom_node_insert_after(node_, node);
    lxb_dom_node_remove(node_);
    return new_node_;
  }

  // Expandable
  std::size_t flat_size() {
    std::vector<std::byte> data;
    zpp::bits::out out(data);
    out(flattened{serialize_doc(), path()}).or_throw();
    return data.size();
  }

  void flatten_into(std::span<std::byte> buffer) {
    zpp::bits::out out(buffer);
    out(flattened{serialize_doc(), path()}).or_throw();
  }
  static cppgres::type type() { return cppgres::named_type("omni_livehtml", type_name); }

  static T restore_from(std::span<std::byte> buffer) {
    zpp::bits::in in(buffer);
    flattened f;
    in(f).or_throw();
    auto *doc = lxb_html_document_create();
    lxb_html_document_parse(doc,
                            reinterpret_cast<lxb_char_t *>(const_cast<char *>(f.document.data())),
                            f.document.size());
    auto node = find_node(doc, f.path);
    if (node.has_value()) {
      return T(*node, doc);
    } else {
      throw std::runtime_error("deserialization error");
    }
  }

  struct flattened {
    std::string document;
    std::vector<std::int64_t> path;
  };

private:
  std::string serialize_doc() {
    str s(doc_);
    lxb_html_serialize_tree_str(lxb_dom_interface_node(doc_), s);
    std::string string(reinterpret_cast<char *>(lexbor_str_data(s)));
    return string;
  }

  // Find node by path
  static std::optional<lxb_dom_node_t *> find_node(lxb_html_document_t *doc_,
                                                   std::vector<std::int64_t> path) {
    auto doc = lxb_dom_interface_document(doc_);
    auto *current = lxb_dom_interface_node(doc)->first_child;

    for (uint32_t target_index : path) {
      uint32_t current_index = 0;
      current = current->first_child;

      while (current && current_index < target_index) {
        current_index++;
        current = current->next;
      }

      if (!current)
        return std::nullopt;
    }

    return current;
  }

protected:
  lxb_dom_node_t *node_;
  lxb_html_document_t *doc_;
};

template <typename T> struct dom_collection {
  dom_collection() = delete;
  explicit dom_collection(auto &doc) : collection_(lxb_dom_collection_make(doc, 1)) {}

  operator lxb_dom_collection_t *() { return collection_.get(); }

  struct iterator {
    lxb_dom_collection_t *col;
    size_t i;

    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T *;
    using reference = T &;

    T operator*() const { return T(lxb_dom_collection_element(col, i)); }

    iterator &operator++() {
      ++i;
      return *this;
    }

    iterator operator++(int) {
      auto tmp = *this;
      ++i;
      return tmp;
    }
    bool operator==(const iterator &o) const { return i == o.i; }
    bool operator!=(const iterator &o) const { return i != o.i; }
  };

  std::size_t length() const { return lxb_dom_collection_length(collection_.get()); }

  iterator begin() const { return {collection_.get(), 0}; }
  iterator end() const { return {collection_.get(), length()}; }

private:
  struct deleter {
    void operator()(lxb_dom_collection_t *coll) { lxb_dom_collection_destroy(coll, true); }
  };
  std::unique_ptr<lxb_dom_collection_t, deleter> collection_;
};

static constexpr const char html_element_type[] = "html_element";

struct element : public node<element, html_element_type> {
  element() = delete;
  using node::node;
  using node::restore_from;

  element(lxb_dom_element_t *element) : node(lxb_dom_interface_node(element)) {}

  template <typename T = element>
  [[nodiscard]]
  auto elements_by_tag_name(std::string qname) {
    dom_collection<T> coll(lxb_dom_interface_element(node_)->node.owner_document);
    size_t len;
    auto name = lxb_dom_element_qualified_name(lxb_dom_interface_element(node_), &len);
    if (std::string_view(reinterpret_cast<const char *>(name), len) == qname) {
      lxb_dom_collection_append(coll, node_);
    }
    lxb_dom_elements_by_tag_name(lxb_dom_interface_element(node_), coll,
                                 reinterpret_cast<lxb_char_t *>(const_cast<char *>(qname.c_str())),
                                 qname.length());
    return coll;
  }

  [[nodiscard]]
  std::string_view tag_name() const {
    std::size_t sz;
    return {reinterpret_cast<const char *>(
        lxb_dom_element_tag_name(lxb_dom_interface_element(node_), &sz))};
  }

  std::optional<std::string_view> id() {
    auto id = lxb_dom_element_id_attribute(lxb_dom_interface_element(node_));
    if (id == nullptr) {
      return std::nullopt;
    }
    std::size_t len;
    auto content = lxb_dom_attr_value(id, &len);
    if (content == nullptr) {
      return std::nullopt;
    }
    return std::string_view(reinterpret_cast<char *>(const_cast<unsigned char *>(content)), len);
  }

  std::optional<std::string_view> attribute(std::string_view name) {
    auto id = lxb_dom_element_attr_by_name(lxb_dom_interface_element(node_),
                                           reinterpret_cast<const unsigned char *>(name.data()),
                                           name.length());
    if (id == nullptr) {
      return std::nullopt;
    }
    std::size_t len;
    auto content = lxb_dom_attr_value(id, &len);
    if (content == nullptr) {
      return std::nullopt;
    }
    return std::string_view(reinterpret_cast<char *>(const_cast<unsigned char *>(content)), len);
  }

  element clone() {
    if (lxb_dom_element_tag_id(lxb_dom_interface_element(node_)) == LXB_TAG_TEMPLATE) {
      return lxb_dom_node_clone(lxb_dom_interface_node(lxb_html_interface_template(node_)->content),
                                true);
    }
    return lxb_dom_node_clone(node_, true);
  }

  struct child_iterator {
    lxb_dom_node_t *current;

    child_iterator(lxb_dom_node_t *node) : current(node) {}

    element operator*() const { return element(current); }

    child_iterator &operator++() {
      if (current) {
        current = lxb_dom_node_next(current);
      }
      return *this;
    }

    // Post-increment
    child_iterator operator++(int) {
      child_iterator temp = *this;
      ++(*this);
      return temp;
    }

    // Equality comparison
    bool operator==(const child_iterator &other) const { return current == other.current; }

    // Inequality comparison
    bool operator!=(const child_iterator &other) const { return current != other.current; }
  };

  child_iterator begin_children() const {
    lxb_dom_node_t *first_child = lxb_dom_node_first_child(node_);
    return child_iterator(first_child);
  }

  child_iterator end_children() const { return child_iterator(nullptr); }

  struct children_range {
    element &elem;
    children_range(element &e) : elem(e) {}

    child_iterator begin() { return elem.begin_children(); }
    child_iterator end() { return elem.end_children(); }
  };

  children_range children() { return children_range(*this); }

  std::optional<std::string_view> text() {
    auto content_node = node_;
    if (lxb_dom_element_tag_id(lxb_dom_interface_element(node_)) == LXB_TAG_TEMPLATE) {
      lxb_html_template_element_t *template_elem = lxb_html_interface_template(node_);
      content_node = lxb_dom_interface_node(template_elem->content);
    }

    std::size_t len;
    auto content = lxb_dom_node_text_content(content_node, &len);
    if (content == nullptr) {
      return std::nullopt;
    }
    return std::string_view(reinterpret_cast<char *>(content), len);
  }

};

struct html_document : element {
  using element::element;

  explicit html_document(lxb_html_document_t *doc_) : element(lxb_dom_interface_node(doc_), doc_) {}
  explicit html_document(lxb_dom_node_t *node_, lxb_html_document_t *doc_) : element(node_, doc_) {}

  explicit html_document()
      : element(lxb_dom_interface_node(&lxb_html_document_create()->dom_document)) {}

  html_document(element e) : element(e) {}

  explicit html_document(std::string_view source) : html_document() {
    lxb_html_document_parse(this->doc_,
                            reinterpret_cast<lxb_char_t *>(const_cast<char *>(source.data())),
                            source.size());
  }

  static html_document restore_from(std::span<std::byte> buffer) {
    return element::restore_from(buffer);
  }

  static cppgres::type type() { return cppgres::named_type("omni_livehtml", "html_document"); }
};

static constexpr const char text_node_type[] = "text_node";
struct text_node : public node<text_node, text_node_type> {
  using node::node;

  text_node(html_document &doc, std::string_view content)
      : node(lxb_dom_interface_node(lxb_dom_document_create_text_node(
            lxb_dom_interface_document(doc.operator lxb_html_document_t *()),
            reinterpret_cast<const unsigned char *>(content.data()), content.length()))) {}
};

cppgres::expanded_varlena<html_document> html_document_in_impl(const char *in) {
  return cppgres::expanded_varlena<html_document>(std::string(in));
}

postgres_function(html_document_in, html_document_in_impl);

const char *html_document_out_impl(cppgres::expanded_varlena<html_document> doc) {
  html_document &html = doc;
  auto s = html.to_string();
  auto allocator = cppgres::memory_context_allocator<char>(cppgres::memory_context(), true);
  char *str = new (allocator.allocate(s.size() + 1)) char[s.size() + 1];
  std::copy(s.c_str(), s.c_str() + s.size() + 1, str);
  return str;
}

postgres_function(html_document_out, html_document_out_impl);

std::string html_document_text_impl(cppgres::expanded_varlena<html_document> doc) {
  html_document &html = doc;
  return html.to_string();
}

postgres_function(html_document_text, html_document_text_impl);

cppgres::expanded_varlena<html_document> render_impl(cppgres::expanded_varlena<html_document> doc) {
  html_document &document = doc;
  std::map<std::string, std::string> queries;

  // Find queries
  for (auto query : document.elements_by_tag_name<element>("omni-query")) {
    auto id = query.id();
    if (!id.has_value()) {
      throw std::runtime_error("omni-query must have id");
    }
    auto script_it = query.elements_by_tag_name("script");
    if (script_it.begin() == script_it.end()) {
      throw std::runtime_error("no valid script found in omni-query");
    }
    element script = (*script_it.begin());
    queries[std::string(*id)] = std::string(script.text().value_or("select"));
    script.remove();
  }
  {
    cppgres::spi_executor spi;
    // Populate omni-for
    for (auto query_for : document.elements_by_tag_name<element>("omni-for")) {
      auto template_it = query_for.elements_by_tag_name("template");
      if (template_it.begin() == template_it.end()) {
        throw std::runtime_error("no valid template found in omni-for");
      }
      element tpl = (*template_it.begin());
      element container = tpl.parent();
      auto query_ref = query_for.attribute("query");
      if (!query_ref.has_value()) {
        throw std::runtime_error("no query found in omni-for");
      }
      auto q = queries.find(std::string(*query_ref));
      if (q == queries.end()) {
        throw std::runtime_error(f::format("query `{}` not found in omni-for", *query_ref));
      }
      auto results = spi.query<std::vector<cppgres::value>>(q->second);
      auto td = results.get_tuple_descriptor();
      using mapping = std::tuple<int, cppgres::function<cppgres::value, const char *>>;
      std::map<std::string, mapping> name_indices;
      for (int i = 0; i < td.attributes(); i++) {
        mapping m{i, cppgres::output_function(td.get_type(i))};
        name_indices.insert({std::string(td.get_name(i)), m});
      }

      for (auto &result : results) {
        auto tpl_clone = tpl.clone();
        for (auto e : tpl_clone.children()) {
          auto new_element = e.clone();
          container.append_child(new_element);
          for (auto slot : new_element.elements_by_tag_name("omni-slot")) {
            std::optional<std::string_view> name = slot.attribute("name");
            if (name.has_value()) {
              auto it = name_indices.find(std::string(*name));
              if (it != name_indices.end()) {
                auto [index, out] = it->second;
                auto text = text_node(document, out(result[index]));
                slot.replace(text);
              } else {
                throw std::runtime_error(f::format("result column {} not found", *name));
              }
            }
          }
        }
      }
    }
  }
  return cppgres::expanded_varlena<html_document>(document);
}

postgres_function(render, render_impl);

void _PG_init(void) {
  lexbor_memory_setup; // TODO
}
