#include <cppgres.hpp>

extern "C" {
  PG_MODULE_MAGIC;

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

}

#include <span>

#include <zpp_bits.h>

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
    cppgres::report(NOTICE, "%s %s", to_string_path().c_str(), string.c_str());
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
  static cppgres::type type() { return cppgres::named_type("omni_html", type_name); }

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
    using reference = T;

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

  template <typename T = element> auto elements_by_tag_name(std::string qname) {
    dom_collection<T> coll(lxb_dom_interface_element(node_)->node.owner_document);
    lxb_dom_elements_by_tag_name(lxb_dom_interface_element(node_), coll,
                                 reinterpret_cast<lxb_char_t *>(const_cast<char *>(qname.c_str())),
                                 qname.length());
    return coll;
  }

  std::string_view tag_name() const {
    std::size_t sz;
    return {reinterpret_cast<const char *>(
        lxb_dom_element_tag_name(lxb_dom_interface_element(node_), &sz))};
  }

  void replace(element &&new_element) {
    if (lxb_html_interface_document(new_element.node_->owner_document) !=
        (lxb_html_interface_document(node_->owner_document))) {
      auto s = new_element.to_string();
      std::span<const char> cs(s.c_str(), s.size());
      new_element = element(std::as_bytes(cs), lxb_html_interface_document(node_->owner_document));
    }
    lxb_dom_node_insert_after(node_, new_element.node_);
    lxb_dom_node_remove(node_);
    node_ = new_element.node_;
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

  static cppgres::type type() { return cppgres::named_type("omni_html", "html_document"); }

  // private:
  //   std::unique_ptr<lxb_html_document_t, void_deleter<lxb_html_document_destroy>> html;
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

cppgres::expanded_varlena<element> html_element_in_impl(const char *in) {
  return cppgres::expanded_varlena<element>(in);
}

postgres_function(html_element_in, html_element_in_impl);

const char *html_element_out_impl(cppgres::expanded_varlena<element> el) {
  element &e = el;
  auto s = e.to_string();
  auto allocator = cppgres::memory_context_allocator<char>(cppgres::memory_context(), true);
  char *str = new (allocator.allocate(s.size() + 1)) char[s.size() + 1];
  std::copy(s.c_str(), s.c_str() + s.size() + 1, str);
  return str;
}

postgres_function(html_element_out, html_element_out_impl);

template <typename T>
dom_collection<cppgres::expanded_varlena<element>>
html_elements_by_tag_name_impl(cppgres::expanded_varlena<T> e, std::string tag_name) {
  element &el = e;
  return el.elements_by_tag_name<cppgres::expanded_varlena<element>>(tag_name);
}

postgres_function(html_elements_by_tag_name, html_elements_by_tag_name_impl<element>);
postgres_function(html_elements_by_tag_name_doc, html_elements_by_tag_name_impl<html_document>);

template <typename T>
cppgres::expanded_varlena<html_document> html_document_impl(cppgres::expanded_varlena<T> e) {
  element &el = e;
  return cppgres::expanded_varlena<html_document>(el.operator lxb_html_document_t *());
}

postgres_function(html_document_get, html_document_impl<element>);

template <typename T>
cppgres::expanded_varlena<element>
html_element_replace_impl(cppgres::expanded_varlena<T> e, cppgres::expanded_varlena<element> e1) {
  element &el = e;
  element &el1 = e1;
  el.replace(std::move(el1));
  return cppgres::expanded_varlena<element>(el);
}

postgres_function(html_element_replace, html_element_replace_impl<element>);

static_assert(cppgres::has_type_traits<cppgres::value>);

cppgres::expanded_varlena<html_document> html_transform_impl(
    cppgres::expanded_varlena<html_document> doc,
    cppgres::function<cppgres::set<cppgres::expanded_varlena<element>>,
                      cppgres::expanded_varlena<html_document>, cppgres::value>
        finder,
    cppgres::function<cppgres::expanded_varlena<element>, cppgres::expanded_varlena<element>>
        transformer,
    cppgres::value arg) {
  html_document &d = doc; // TODO: this should be fixed on cppgres side? can't call finder(doc)
                          // before I do this expansion – maybe if it wasn't expanded but varatt
                          // just return what we have??
  for (auto &elt : finder(doc, arg)) {
    element &e = elt;
    auto new_elt = transformer(elt);
    e.replace(std::move(new_elt.operator element &()));
  }
  return doc;
}

static_assert(cppgres::convertible_from_nullable_datum<
              cppgres::function<cppgres::set<cppgres::expanded_varlena<element>>,
                                cppgres::expanded_varlena<html_document>, cppgres::value>>);

postgres_function(html_transform, html_transform_impl);

void _PG_init(void) {
  lexbor_memory_setup; // TODO
}
